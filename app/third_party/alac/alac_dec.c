/*
 * ALAC decoder core, ported to C for neuPod.
 *
 * Derived from ALACDecoder.cpp (Apple Lossless Audio Codec, https://github.com/macosforge/alac, commit
 * c38887c5c5e64a4b31108733bd79ca9b2496d987), Copyright (c) 2011 Apple Inc., licensed under the Apache License,
 * Version 2.0 (see LICENSE in this directory).
 *
 * MODIFICATIONS (Apache-2.0 section 4(b)): converted from a C++ class to a C struct with the same decoding logic;
 * a partial-frame sample count larger than frameLength is rejected (the original trusted it and could write past its buffers); buffers come from a caller-supplied allocator instead of calloc (no heap on the bare-metal audio core); the
 * magic-cookie parsing and the unused channel-layout parts are reduced to what the decoder needs; output is
 * written to a caller buffer in the packed 16/20/24/32-bit layouts produced by the original matrix routines.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software distributed under the License is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for
 * the specific language governing permissions and limitations under the License.
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */
#include <stdbool.h>
#include <string.h>

#include "alac_dec.h"
#include "aglib.h"
#include "dplib.h"
#include "matrixlib.h"
#include "ALACBitUtilities.h"
#include "EndianPortable.h"

/*
	ZeroN()
	- helper routines to clear out output channel buffers when decoding fewer channels than requested
*/
static void Zero16( int16_t * buffer, uint32_t numItems, uint32_t stride )
{
	if ( stride == 1 )
	{
		memset( buffer, 0, numItems * sizeof(int16_t) );
	}
	else
	{
		for ( uint32_t index = 0; index < (numItems * stride); index += stride )
			buffer[index] = 0;
	}
}

static void Zero24( uint8_t * buffer, uint32_t numItems, uint32_t stride )
{
	if ( stride == 1 )
	{
		memset( buffer, 0, numItems * 3 );
	}
	else
	{
		for ( uint32_t index = 0; index < (numItems * stride * 3); index += (stride * 3) )
		{
			buffer[index + 0] = 0;
			buffer[index + 1] = 0;
			buffer[index + 2] = 0;
		}
	}
}

static void Zero32( int32_t * buffer, uint32_t numItems, uint32_t stride )
{
	if ( stride == 1 )
	{
		memset( buffer, 0, numItems * sizeof(int32_t) );
	}
	else
	{
		for ( uint32_t index = 0; index < (numItems * stride); index += stride )
			buffer[index] = 0;
	}
}

/*
	FillElement()
	- they're just filler so we don't need 'em
*/
static int32_t FillElement( BitBuffer * bits )
{
	int16_t		count;
	
	// 4-bit count or (4-bit + 8-bit count) if 4-bit count == 15
	// - plus this weird -1 thing I still don't fully understand
	count = BitBufferReadSmall( bits, 4 );
	if ( count == 15 )
		count += (int16_t) BitBufferReadSmall( bits, 8 ) - 1;

	BitBufferAdvance( bits, count * 8 );

	RequireAction( bits->cur <= bits->end, return kALAC_ParamError; );

	return ALAC_noErr;	
}

/*
	DataStreamElement()
	- we don't care about data stream elements so just skip them
*/
static int32_t DataStreamElement( BitBuffer * bits )
{
	uint8_t		element_instance_tag;
	int32_t		data_byte_align_flag;
	uint16_t		count;
	
	// the tag associates this data stream element with a given audio element
	element_instance_tag = BitBufferReadSmall( bits, 4 );
	
	data_byte_align_flag = BitBufferReadOne( bits );

	// 8-bit count or (8-bit + 8-bit count) if 8-bit count == 255
	count = BitBufferReadSmall( bits, 8 );
	if ( count == 255 )
		count += BitBufferReadSmall( bits, 8 );

	// the align flag means the bitstream should be byte-aligned before reading the following data bytes
	if ( data_byte_align_flag )
		BitBufferByteAlign( bits, false );

	// skip the data bytes
	BitBufferAdvance( bits, count * 8 );

	RequireAction( bits->cur <= bits->end, return kALAC_ParamError; );

	return ALAC_noErr;
}


static int32_t Decode( alac_dec_t * d, BitBuffer * bits, uint8_t * sampleBuffer, uint32_t numSamples, uint32_t numChannels, uint32_t * outNumSamples )
{
	BitBuffer			shiftBits;
	uint32_t            bits1, bits2;
	uint8_t				tag;
	uint8_t				elementInstanceTag;
	AGParamRec			agParams;
	uint32_t				channelIndex;
	int16_t				coefsU[32];		// max possible size is 32 although NUMCOEPAIRS is the current limit
	int16_t				coefsV[32];
	uint8_t				numU, numV;
	uint8_t				mixBits;
	int8_t				mixRes;
	uint16_t			unusedHeader;
	uint8_t				escapeFlag;
	uint32_t			chanBits;
	uint8_t				bytesShifted;
	uint32_t			shift;
	uint8_t				modeU, modeV;
	uint32_t			denShiftU, denShiftV;
	uint16_t			pbFactorU, pbFactorV;
	uint16_t			pb;
	int16_t *			samples;
	int16_t *			out16;
	uint8_t *			out20;
	uint8_t *			out24;
	int32_t *			out32;
	uint8_t				headerByte;
	uint8_t				partialFrame;
	uint32_t			extraBits;
	int32_t				val;
	uint32_t			i, j;
	int32_t             status;
	
	RequireAction( (bits != nil) && (sampleBuffer != nil) && (outNumSamples != nil), return kALAC_ParamError; );
	RequireAction( numChannels > 0, return kALAC_ParamError; );

	channelIndex	= 0;
	
	samples = (int16_t *) sampleBuffer;

	status = ALAC_noErr;
	*outNumSamples = numSamples;

	while ( status == ALAC_noErr )
	{
		// bail if we ran off the end of the buffer
    	RequireAction( bits->cur < bits->end, status = kALAC_ParamError; goto Exit; );

		// copy global decode params for this element
		pb = d->cfg.pb;

		// read element tag
		tag = BitBufferReadSmall( bits, 3 );
		switch ( tag )
		{
			case ID_SCE:
			case ID_LFE:
			{
				// mono/LFE channel
				elementInstanceTag = BitBufferReadSmall( bits, 4 );
				(void)elementInstanceTag;

				// read the 12 unused header bits
				unusedHeader = (uint16_t) BitBufferRead( bits, 12 );
				RequireAction( unusedHeader == 0, status = kALAC_ParamError; goto Exit; );

				// read the 1-bit "partial frame" flag, 2-bit "shift-off" flag & 1-bit "escape" flag
				headerByte = (uint8_t) BitBufferRead( bits, 4 );
				
				partialFrame = headerByte >> 3;
				
				bytesShifted = (headerByte >> 1) & 0x3u;
				RequireAction( bytesShifted != 3, status = kALAC_ParamError; goto Exit; );

				shift = bytesShifted * 8;

				escapeFlag = headerByte & 0x1;

				chanBits = d->cfg.bitDepth - (bytesShifted * 8);
				
				// check for partial frame to override requested numSamples
				if ( partialFrame != 0 )
				{
					numSamples  = BitBufferRead( bits, 16 ) << 16;
					numSamples |= BitBufferRead( bits, 16 );
					// neuPod: the count comes from the file; never let it exceed the buffers sized from frameLength
					RequireAction( numSamples <= d->cfg.frameLength, status = kALAC_ParamError; goto Exit; );
				}

				if ( escapeFlag == 0 )
				{
					// compressed frame, read rest of parameters
					mixBits	= (uint8_t) BitBufferRead( bits, 8 );
					mixRes	= (int8_t) BitBufferRead( bits, 8 );
					//Assert( (mixBits == 0) && (mixRes == 0) );		// no mixing for mono

					headerByte	= (uint8_t) BitBufferRead( bits, 8 );
					modeU		= headerByte >> 4;
					denShiftU	= headerByte & 0xfu;
					
					headerByte	= (uint8_t) BitBufferRead( bits, 8 );
					pbFactorU	= headerByte >> 5;
					numU		= headerByte & 0x1fu;

					for ( i = 0; i < numU; i++ )
						coefsU[i] = (int16_t) BitBufferRead( bits, 16 );
					
					// if shift active, skip the the shift buffer but remember where it starts
					if ( bytesShifted != 0 )
					{
						shiftBits = *bits;
						BitBufferAdvance( bits, (bytesShifted * 8) * numSamples ); 
					}

					// decompress
					set_ag_params( &agParams, d->cfg.mb, (pb * pbFactorU) / 4, d->cfg.kb, numSamples, numSamples, d->cfg.maxRun );
					status = dyn_decomp( &agParams, bits, d->pred, numSamples, chanBits, &bits1 );
					RequireNoErr( status, goto Exit; );

					if ( modeU == 0 )
					{
						unpc_block( d->pred, d->mixU, numSamples, &coefsU[0], numU, chanBits, denShiftU );
					}
					else
					{
						// the special "numActive == 31" mode can be done in-place
						unpc_block( d->pred, d->pred, numSamples, nil, 31, chanBits, 0 );
						unpc_block( d->pred, d->mixU, numSamples, &coefsU[0], numU, chanBits, denShiftU );
					}
				}
				else
				{
					//Assert( bytesShifted == 0 );

					// uncompressed frame, copy data into the mix buffer to use common output code
					shift = 32 - chanBits;
					if ( chanBits <= 16 )
					{
						for ( i = 0; i < numSamples; i++ )
						{
							val = (int32_t) BitBufferRead( bits, (uint8_t) chanBits );
							val = (val << shift) >> shift;
							d->mixU[i] = val;
						}
					}
					else
					{
						// BitBufferRead() can't read more than 16 bits at a time so break up the reads
						extraBits = chanBits - 16;
						for ( i = 0; i < numSamples; i++ )
						{
							val = (int32_t) BitBufferRead( bits, 16 );
							val = (val << 16) >> shift;
							d->mixU[i] = val | BitBufferRead( bits, (uint8_t) extraBits );
						}
					}

					mixBits = mixRes = 0;
					bits1 = chanBits * numSamples;
					bytesShifted = 0;
				}

				// now read the shifted values into the shift buffer
				if ( bytesShifted != 0 )
				{
					shift = bytesShifted * 8;
					//Assert( shift <= 16 );

					for ( i = 0; i < numSamples; i++ )
						d->shift[i] = (uint16_t) BitBufferRead( &shiftBits, (uint8_t) shift );
				}

				// convert 32-bit integers into output buffer
				switch ( d->cfg.bitDepth )
				{
					case 16:
						out16 = &((int16_t *)sampleBuffer)[channelIndex];
						for ( i = 0, j = 0; i < numSamples; i++, j += numChannels )
							out16[j] = (int16_t) d->mixU[i];
						break;
					case 20:
						out20 = (uint8_t *)sampleBuffer + (channelIndex * 3);
						copyPredictorTo20( d->mixU, out20, numChannels, numSamples );
						break;
					case 24:
						out24 = (uint8_t *)sampleBuffer + (channelIndex * 3);
						if ( bytesShifted != 0 )
							copyPredictorTo24Shift( d->mixU, d->shift, out24, numChannels, numSamples, bytesShifted );
						else
							copyPredictorTo24( d->mixU, out24, numChannels, numSamples );							
						break;
					case 32:
						out32 = &((int32_t *)sampleBuffer)[channelIndex];
						if ( bytesShifted != 0 )
							copyPredictorTo32Shift( d->mixU, d->shift, out32, numChannels, numSamples, bytesShifted );
						else
							copyPredictorTo32( d->mixU, out32, numChannels, numSamples);
						break;
				}

				channelIndex += 1;
				*outNumSamples = numSamples;
				break;
			}

			case ID_CPE:
			{
				// if decoding this pair would take us over the max channels limit, bail
				if ( (channelIndex + 2) > numChannels )
					goto NoMoreChannels;

				// stereo channel pair
				elementInstanceTag = BitBufferReadSmall( bits, 4 );
				(void)elementInstanceTag;

				// read the 12 unused header bits
				unusedHeader = (uint16_t) BitBufferRead( bits, 12 );
				RequireAction( unusedHeader == 0, status = kALAC_ParamError; goto Exit; );

				// read the 1-bit "partial frame" flag, 2-bit "shift-off" flag & 1-bit "escape" flag
				headerByte = (uint8_t) BitBufferRead( bits, 4 );
				
				partialFrame = headerByte >> 3;
				
				bytesShifted = (headerByte >> 1) & 0x3u;
				RequireAction( bytesShifted != 3, status = kALAC_ParamError; goto Exit; );

				shift = bytesShifted * 8;

				escapeFlag = headerByte & 0x1;

				chanBits = d->cfg.bitDepth - (bytesShifted * 8) + 1;
				
				// check for partial frame length to override requested numSamples
				if ( partialFrame != 0 )
				{
					numSamples  = BitBufferRead( bits, 16 ) << 16;
					numSamples |= BitBufferRead( bits, 16 );
					// neuPod: the count comes from the file; never let it exceed the buffers sized from frameLength
					RequireAction( numSamples <= d->cfg.frameLength, status = kALAC_ParamError; goto Exit; );
				}

				if ( escapeFlag == 0 )
				{
					// compressed frame, read rest of parameters
					mixBits		= (uint8_t) BitBufferRead( bits, 8 );
					mixRes		= (int8_t) BitBufferRead( bits, 8 );

					headerByte	= (uint8_t) BitBufferRead( bits, 8 );
					modeU		= headerByte >> 4;
					denShiftU	= headerByte & 0xfu;
					
					headerByte	= (uint8_t) BitBufferRead( bits, 8 );
					pbFactorU	= headerByte >> 5;
					numU		= headerByte & 0x1fu;
					for ( i = 0; i < numU; i++ )
						coefsU[i] = (int16_t) BitBufferRead( bits, 16 );

					headerByte	= (uint8_t) BitBufferRead( bits, 8 );
					modeV		= headerByte >> 4;
					denShiftV	= headerByte & 0xfu;
					
					headerByte	= (uint8_t) BitBufferRead( bits, 8 );
					pbFactorV	= headerByte >> 5;
					numV		= headerByte & 0x1fu;
					for ( i = 0; i < numV; i++ )
						coefsV[i] = (int16_t) BitBufferRead( bits, 16 );

					// if shift active, skip the interleaved shifted values but remember where they start
					if ( bytesShifted != 0 )
					{
						shiftBits = *bits;
						BitBufferAdvance( bits, (bytesShifted * 8) * 2 * numSamples );
					}

					// decompress and run predictor for "left" channel
					set_ag_params( &agParams, d->cfg.mb, (pb * pbFactorU) / 4, d->cfg.kb, numSamples, numSamples, d->cfg.maxRun );
					status = dyn_decomp( &agParams, bits, d->pred, numSamples, chanBits, &bits1 );
					RequireNoErr( status, goto Exit; );

					if ( modeU == 0 )
					{
						unpc_block( d->pred, d->mixU, numSamples, &coefsU[0], numU, chanBits, denShiftU );
					}
					else
					{
						// the special "numActive == 31" mode can be done in-place
						unpc_block( d->pred, d->pred, numSamples, nil, 31, chanBits, 0 );
						unpc_block( d->pred, d->mixU, numSamples, &coefsU[0], numU, chanBits, denShiftU );
					}

					// decompress and run predictor for "right" channel
					set_ag_params( &agParams, d->cfg.mb, (pb * pbFactorV) / 4, d->cfg.kb, numSamples, numSamples, d->cfg.maxRun );
					status = dyn_decomp( &agParams, bits, d->pred, numSamples, chanBits, &bits2 );
					RequireNoErr( status, goto Exit; );

					if ( modeV == 0 )
					{
						unpc_block( d->pred, d->mixV, numSamples, &coefsV[0], numV, chanBits, denShiftV );
					}
					else
					{
						// the special "numActive == 31" mode can be done in-place
						unpc_block( d->pred, d->pred, numSamples, nil, 31, chanBits, 0 );
						unpc_block( d->pred, d->mixV, numSamples, &coefsV[0], numV, chanBits, denShiftV );
					}
				}
				else
				{
					//Assert( bytesShifted == 0 );

					// uncompressed frame, copy data into the mix buffers to use common output code
					chanBits = d->cfg.bitDepth;
					shift = 32 - chanBits;
					if ( chanBits <= 16 )
					{
						for ( i = 0; i < numSamples; i++ )
						{
							val = (int32_t) BitBufferRead( bits, (uint8_t) chanBits );
							val = (val << shift) >> shift;
							d->mixU[i] = val;

							val = (int32_t) BitBufferRead( bits, (uint8_t) chanBits );
							val = (val << shift) >> shift;
							d->mixV[i] = val;
						}
					}
					else
					{
						// BitBufferRead() can't read more than 16 bits at a time so break up the reads
						extraBits = chanBits - 16;
						for ( i = 0; i < numSamples; i++ )
						{
							val = (int32_t) BitBufferRead( bits, 16 );
							val = (val << 16) >> shift;
							d->mixU[i] = val | BitBufferRead( bits, (uint8_t)extraBits );

							val = (int32_t) BitBufferRead( bits, 16 );
							val = (val << 16) >> shift;
							d->mixV[i] = val | BitBufferRead( bits, (uint8_t)extraBits );
						}
					}

					bits1 = chanBits * numSamples;
					bits2 = chanBits * numSamples;
					mixBits = mixRes = 0;
					bytesShifted = 0;
				}

				// now read the shifted values into the shift buffer
				if ( bytesShifted != 0 )
				{
					shift = bytesShifted * 8;
					//Assert( shift <= 16 );

					for ( i = 0; i < (numSamples * 2); i += 2 )
					{
						d->shift[i + 0] = (uint16_t) BitBufferRead( &shiftBits, (uint8_t) shift );
						d->shift[i + 1] = (uint16_t) BitBufferRead( &shiftBits, (uint8_t) shift );
					}
				}

				// un-mix the data and convert to output format
				// - note that mixRes = 0 means just interleave so we use that path for uncompressed frames
				switch ( d->cfg.bitDepth )
				{
					case 16:
						out16 = &((int16_t *)sampleBuffer)[channelIndex];
						unmix16( d->mixU, d->mixV, out16, numChannels, numSamples, mixBits, mixRes );
						break;
					case 20:
						out20 = (uint8_t *)sampleBuffer + (channelIndex * 3);
						unmix20( d->mixU, d->mixV, out20, numChannels, numSamples, mixBits, mixRes );
						break;
					case 24:
						out24 = (uint8_t *)sampleBuffer + (channelIndex * 3);
						unmix24( d->mixU, d->mixV, out24, numChannels, numSamples,
									mixBits, mixRes, d->shift, bytesShifted );
						break;
					case 32:
						out32 = &((int32_t *)sampleBuffer)[channelIndex];
						unmix32( d->mixU, d->mixV, out32, numChannels, numSamples,
									mixBits, mixRes, d->shift, bytesShifted );
						break;
				}

				channelIndex += 2;
				*outNumSamples = numSamples;
				break;
			}

			case ID_CCE:
			case ID_PCE:
			{
				// unsupported element, bail
				//AssertNoErr( tag );
				status = kALAC_ParamError;
				break;
			}

			case ID_DSE:
			{
				// data stream element -- parse but ignore
				status = DataStreamElement( bits );
				break;
			}
			
			case ID_FIL:
			{
				// fill element -- parse but ignore
				status = FillElement( bits );
				break;
			}

			case ID_END:
			{
				// frame end, all done so byte align the frame and check for overruns
				BitBufferByteAlign( bits, false );
				//Assert( bits->cur == bits->end );
				goto Exit;
			}
		}

#if ! DEBUG
		// if we've decoded all of our channels, bail (but not in debug b/c we want to know if we're seeing bad bits)
		// - this also protects us if the config does not match the bitstream or crap data bits follow the audio bits
		if ( channelIndex >= numChannels )
			break;
#endif
	}

NoMoreChannels:

	// if we get here and haven't decoded all of the requested channels, fill the remaining channels with zeros
	for ( ; channelIndex < numChannels; channelIndex++ )
	{
		switch ( d->cfg.bitDepth )
		{
			case 16:
			{
				int16_t *	fill16 = &((int16_t *)sampleBuffer)[channelIndex];
				Zero16( fill16, numSamples, numChannels );
				break;
			}
			case 24:
			{
				uint8_t *	fill24 = (uint8_t *)sampleBuffer + (channelIndex * 3);
				Zero24( fill24, numSamples, numChannels );
				break;
			}
			case 32:
			{
				int32_t *	fill32 = &((int32_t *)sampleBuffer)[channelIndex];
				Zero32( fill32, numSamples, numChannels );
				break;
			}
		}
	}

Exit:
	return status;
}

#if PRAGMA_MARK
#pragma mark -
#endif


int32_t alac_dec_init( alac_dec_t * d, const uint8_t * cookie, uint32_t cookieSize, void * (*alloc)(size_t) )
{
    const uint8_t * c = cookie;
    uint32_t remaining = cookieSize;
    memset( d, 0, sizeof(*d) );
    if ( cookieSize < 12 + sizeof(ALACSpecificConfig) - 12 )
        return kALAC_ParamError;
    // Optional 'frma' and 'alac' atom wrappers precede the ALACSpecificConfig (ALACMagicCookieDescription.txt).
    if ( remaining >= 12 && c[4] == 'f' && c[5] == 'r' && c[6] == 'm' && c[7] == 'a' ) { c += 12; remaining -= 12; }
    if ( remaining >= 12 && c[4] == 'a' && c[5] == 'l' && c[6] == 'a' && c[7] == 'c' ) { c += 12; remaining -= 12; }
    if ( remaining < sizeof(ALACSpecificConfig) )
        return kALAC_ParamError;
    const ALACSpecificConfig * in = (const ALACSpecificConfig *) c;
    d->cfg.frameLength = Swap32BtoN( in->frameLength );
    d->cfg.compatibleVersion = in->compatibleVersion;
    d->cfg.bitDepth = in->bitDepth;
    d->cfg.pb = in->pb;
    d->cfg.mb = in->mb;
    d->cfg.kb = in->kb;
    d->cfg.numChannels = in->numChannels;
    d->cfg.maxRun = Swap16BtoN( in->maxRun );
    d->cfg.maxFrameBytes = Swap32BtoN( in->maxFrameBytes );
    d->cfg.avgBitRate = Swap32BtoN( in->avgBitRate );
    d->cfg.sampleRate = Swap32BtoN( in->sampleRate );
    if ( d->cfg.compatibleVersion > kALACVersion )
        return kALAC_ParamError;
    if ( d->cfg.frameLength == 0 || d->cfg.frameLength > 16384 || d->cfg.numChannels < 1 || d->cfg.numChannels > 2 )
        return kALAC_ParamError;
    if ( d->cfg.bitDepth != 16 && d->cfg.bitDepth != 20 && d->cfg.bitDepth != 24 && d->cfg.bitDepth != 32 )
        return kALAC_ParamError;
    d->mixU = (int32_t *) alloc( d->cfg.frameLength * sizeof(int32_t) );
    d->mixV = (int32_t *) alloc( d->cfg.frameLength * sizeof(int32_t) );
    d->pred = (int32_t *) alloc( d->cfg.frameLength * sizeof(int32_t) );
    if ( d->mixU == NULL || d->mixV == NULL || d->pred == NULL )
        return kALAC_MemFullError;
    memset( d->mixU, 0, d->cfg.frameLength * sizeof(int32_t) );
    memset( d->mixV, 0, d->cfg.frameLength * sizeof(int32_t) );
    memset( d->pred, 0, d->cfg.frameLength * sizeof(int32_t) );
    d->shift = (uint16_t *) d->pred; // the "shift off" buffer shares memory with the predictor buffer, as in the original
    return ALAC_noErr;
}

uint32_t alac_dec_out_bytes_per_frame( const alac_dec_t * d )
{
    uint32_t bytes = d->cfg.bitDepth == 16 ? 2 : d->cfg.bitDepth == 32 ? 4 : 3;
    return bytes * d->cfg.numChannels;
}

int32_t alac_dec_decode( alac_dec_t * d, const uint8_t * in, uint32_t inBytes, uint8_t * out, uint32_t * outFrames )
{
    BitBuffer bits;
    BitBufferInit( &bits, (uint8_t *) in, inBytes );
    return Decode( d, &bits, out, d->cfg.frameLength, d->cfg.numChannels, outFrames );
}
