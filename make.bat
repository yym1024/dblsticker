@echo off
set base=D:\WinDDK\7.16
set bin=%base%\bin\x86
set inc=%base%\inc
set lib=%base%\lib

%bin%\rc.exe /I"%inc%\api" /fo resource.res dblsticker\resource.rc
%bin%\x86\cl.exe /MD /Ox /GL /GS- /D NDEBUG /D UNICODE /D _UNICODE /D nullptr=0 dblsticker\main.cpp /I "%inc%\api" /I "%inc%\crt" /link /libpath:"%lib%\win7\i386" /libpath:"%lib%\crt\i386" kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib resource.res /out:dblsticker.exe /MACHINE:X86 /SubSystem:Windows /OPT:REF
%bin%\mt.exe /manifest dblsticker.dpi.manifest /outputresource:dblsticker.exe;#1
%bin%\mt.exe /manifest dblsticker.exe.manifest /updateresource:dblsticker.exe;#1
pause
