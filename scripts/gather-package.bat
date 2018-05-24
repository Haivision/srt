rem Create empty directories for package bundle
@echo off

md %APPVEYOR_BUILD_FOLDER%\package
md %APPVEYOR_BUILD_FOLDER%\package\include
md %APPVEYOR_BUILD_FOLDER%\package\include\win
md %APPVEYOR_BUILD_FOLDER%\package\bin        
md %APPVEYOR_BUILD_FOLDER%\package\lib
md %APPVEYOR_BUILD_FOLDER%\package\pthread-win32
md %APPVEYOR_BUILD_FOLDER%\package\openssl-win64

rem Gather SRT includes, binaries and libs
copy %APPVEYOR_BUILD_FOLDER%\version.h %APPVEYOR_BUILD_FOLDER%\package\include\
copy %APPVEYOR_BUILD_FOLDER%\srtcore\*.h %APPVEYOR_BUILD_FOLDER%\package\include\
copy %APPVEYOR_BUILD_FOLDER%\haicrypt\*.h %APPVEYOR_BUILD_FOLDER%\package\include\
copy %APPVEYOR_BUILD_FOLDER%\common\*.h %APPVEYOR_BUILD_FOLDER%\package\include\
copy %APPVEYOR_BUILD_FOLDER%\common\win\*.h %APPVEYOR_BUILD_FOLDER%\package\include\win\
copy %APPVEYOR_BUILD_FOLDER%\%CONFIGURATION%\*.exe %APPVEYOR_BUILD_FOLDER%\package\bin\
copy %APPVEYOR_BUILD_FOLDER%\%CONFIGURATION%\*.dll %APPVEYOR_BUILD_FOLDER%\package\bin\
copy %APPVEYOR_BUILD_FOLDER%\%CONFIGURATION%\*.lib %APPVEYOR_BUILD_FOLDER%\package\lib\
copy %APPVEYOR_BUILD_FOLDER%\%CONFIGURATION%\*.lib %APPVEYOR_BUILD_FOLDER%\package\lib\

rem gather 3rd party elements
(robocopy c:\openssl-win64\ %APPVEYOR_BUILD_FOLDER%\package\openssl-win64 /s /e /np) ^& IF %ERRORLEVEL% GTR 1 exit %ERRORLEVEL%
(robocopy c:\pthread-win32\ %APPVEYOR_BUILD_FOLDER%\package\pthread-win32 /s /e /np) ^& IF %ERRORLEVEL% GTR 1 exit %ERRORLEVEL%
exit 0
