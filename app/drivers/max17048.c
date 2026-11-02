// SPDX-License-Identifier: MPL-2.0
// MAX17048 fuel gauge: implements hal_battery.h on top of hal_i2c.
#include "max17048.h"

#include "hal_battery.h"
#include "hal_i2c.h"
#include "hal_system.h"

#define RESET_SETTLE_MS 250u // wait after power-on-reset before talking to the chip
#define RESET_POLL_MS 10u
#define RESET_TIMEOUT_MS 500u

uint32_t max17048_vcell_to_mv(uint16_t raw) { return ((uint32_t)raw * 5u + 32u) / 64u; }

uint8_t max17048_soc_to_pct(uint16_t raw) {
    uint32_t pct = raw >> 8;
    return (uint8_t)(pct > 100u ? 100u : pct);
}

int32_t max17048_crate_to_mpct_per_h(uint16_t raw) { return (int32_t)(int16_t)raw * 208; }

uint8_t max17048_status_to_alerts(uint16_t status) {
    return (uint8_t)((status & MAX17048_STATUS_ALERT_MASK) >> 8);
}

uint16_t max17048_config_set_athd(uint16_t config, uint8_t pct) {
    if (pct < 1u) {
        pct = 1u;
    }
    if (pct > 32u) {
        pct = 32u;
    }
    return (uint16_t)((config & ~MAX17048_CONFIG_ATHD_MASK) | (uint16_t)(32u - pct));
}

uint8_t max17048_config_athd_pct(uint16_t config) {
    return (uint8_t)(32u - (config & MAX17048_CONFIG_ATHD_MASK));
}

void max17048_hibrt_decode(uint16_t hibrt, uint32_t *hib_mpct_per_h, uint32_t *act_mv) {
    *hib_mpct_per_h = (uint32_t)(hibrt >> 8) * 208u;
    *act_mv = (uint32_t)(hibrt & 0xFFu) * 5u / 4u;
}

uint16_t max17048_hibrt_encode(uint32_t hib_mpct_per_h, uint32_t act_mv) {
    uint32_t hib = hib_mpct_per_h / 208u;
    uint32_t act = act_mv * 4u / 5u; // 1.25 mV per LSB
    if (hib > 255u) {
        hib = 255u;
    }
    if (act > 255u) {
        act = 255u;
    }
    return (uint16_t)((hib << 8) | act);
}

static hal_status_t map(hal_i2c_err_t e) {
    switch (e) {
    case HAL_I2C_OK: return HAL_OK;
    case HAL_I2C_NACK_ADDR: return HAL_ENODEV;
    case HAL_I2C_TIMEOUT: return HAL_ETIMEOUT;
    case HAL_I2C_BAD_PARAM: return HAL_EINVAL;
    default: return HAL_ERR;
    }
}

static hal_status_t rd(uint8_t reg, uint16_t *v) {
    return map(hal_i2c_reg_read16_be(MAX17048_ADDR, reg, v));
}

static hal_status_t wr(uint8_t reg, uint16_t v) {
    return map(hal_i2c_reg_write16_be(MAX17048_ADDR, reg, v));
}

hal_status_t hal_battery_get_version(uint16_t *version) { return rd(MAX17048_REG_VERSION, version); }
hal_status_t hal_battery_get_config(uint16_t *config) { return rd(MAX17048_REG_CONFIG, config); }
hal_status_t hal_battery_get_hibrt(uint16_t *hibrt) { return rd(MAX17048_REG_HIBRT, hibrt); }

// Version check only (no side effects).
static hal_status_t probe_version(void) {
    uint16_t ver;
    hal_status_t st = hal_battery_get_version(&ver);
    if (st != HAL_OK) {
        return st;
    }
    return (ver & MAX17048_VERSION_MASK) == MAX17048_VERSION_VALUE ? HAL_OK : HAL_ENODEV;
}

hal_status_t hal_battery_init(void) {
    hal_status_t st = probe_version();
    if (st != HAL_OK) {
        return st;
    }
    // Acknowledge the power-up/reset indicator (STATUS.RI) so later alert flags are meaningful.
    // Other flags are left as read. Failure to clear is not an init failure.
    uint16_t status;
    if (rd(MAX17048_REG_STATUS, &status) == HAL_OK && (status & MAX17048_STATUS_RI)) {
        (void)wr(MAX17048_REG_STATUS, (uint16_t)(status & ~MAX17048_STATUS_RI));
    }
    return HAL_OK;
}

hal_status_t hal_battery_read(hal_battery_status_t *out) {
    if (!out) {
        return HAL_EINVAL;
    }
    *out = (hal_battery_status_t){0};
    uint16_t vcell, soc, crate, status;
    hal_status_t st = rd(MAX17048_REG_VCELL, &vcell);
    if (st == HAL_OK) {
        st = rd(MAX17048_REG_SOC, &soc);
    }
    if (st == HAL_OK) {
        st = rd(MAX17048_REG_CRATE, &crate);
    }
    if (st == HAL_OK) {
        st = rd(MAX17048_REG_STATUS, &status);
    }
    if (st != HAL_OK) {
        return st;
    }
    out->mv = (uint16_t)max17048_vcell_to_mv(vcell);
    out->soc_x256 = soc;
    out->soc_pct = max17048_soc_to_pct(soc);
    out->rate_mpct_per_h = max17048_crate_to_mpct_per_h(crate);
    out->alerts = max17048_status_to_alerts(status);
    out->valid = true;
    out->present = out->mv >= HAL_BATTERY_PRESENT_MV;
    return HAL_OK;
}

hal_status_t hal_battery_set_alert_threshold(uint8_t pct) {
    if (pct < 1u || pct > 32u) {
        return HAL_EINVAL;
    }
    uint16_t cfg;
    hal_status_t st = rd(MAX17048_REG_CONFIG, &cfg);
    return st == HAL_OK ? wr(MAX17048_REG_CONFIG, max17048_config_set_athd(cfg, pct)) : st;
}

hal_status_t hal_battery_clear_alert(void) {
    uint16_t cfg, status;
    hal_status_t st = rd(MAX17048_REG_CONFIG, &cfg);
    if (st == HAL_OK) {
        st = rd(MAX17048_REG_STATUS, &status);
    }
    if (st == HAL_OK) {
        st = wr(MAX17048_REG_CONFIG, (uint16_t)(cfg & ~MAX17048_CONFIG_ALRT));
    }
    if (st == HAL_OK) {
        st = wr(MAX17048_REG_STATUS, (uint16_t)(status & MAX17048_STATUS_ENVR));
    }
    return st;
}

hal_status_t hal_battery_quick_start(void) { return wr(MAX17048_REG_MODE, MAX17048_MODE_QUICKSTART); }

hal_status_t hal_battery_set_hibernate(uint32_t hib_mpct_per_h, uint32_t act_mv) {
    return wr(MAX17048_REG_HIBRT, max17048_hibrt_encode(hib_mpct_per_h, act_mv));
}

hal_status_t hal_battery_reset(void) {
    hal_status_t st = wr(MAX17048_REG_CMD, MAX17048_CMD_POR);
    if (st != HAL_OK && st != HAL_ENODEV && st != HAL_ERR) {
        return st; // a NACK (mapped to ENODEV / ERR) is the chip resetting under us: expected
    }
    hal_delay_ms(RESET_SETTLE_MS);
    for (uint32_t t = 0; t <= RESET_TIMEOUT_MS; t += RESET_POLL_MS) {
        if (probe_version() == HAL_OK) { // does not touch STATUS: RI stays visible after a reset
            return HAL_OK;
        }
        hal_delay_ms(RESET_POLL_MS);
    }
    return HAL_ETIMEOUT;
}
