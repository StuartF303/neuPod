# .NET on the Y2: no ICU on the device, and the 3.4 kernel lacks memfd_create (needed by W^X double mapping).
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
export DOTNET_EnableWriteXorExecute=0
