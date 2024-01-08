#Script to grab external libraries for building these samples

# prep shared variables
$DEVENV_PLATFORM = 'x64' # alternative is x86
$FOLDER_PLATFORM = 'win64' # alternative is win32
nuget install swigwintools -version 4.0.0