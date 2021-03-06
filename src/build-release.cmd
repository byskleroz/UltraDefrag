@echo off
::
:: This script builds UltraDefrag releases.
:: Copyright (c) 2007-2015 Dmitri Arkhangelski (dmitriar@gmail.com).
:: Copyright (c) 2011-2013 Stefan Pendl (stefanpe@users.sourceforge.net).
::
:: This program is free software; you can redistribute it and/or modify
:: it under the terms of the GNU General Public License as published by
:: the Free Software Foundation; either version 2 of the License, or
:: (at your option) any later version.
::
:: This program is distributed in the hope that it will be useful,
:: but WITHOUT ANY WARRANTY; without even the implied warranty of
:: MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
:: GNU General Public License for more details.
::
:: You should have received a copy of the GNU General Public License
:: along with this program; if not, write to the Free Software
:: Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
::

call build.cmd --clean

rd /s /q release
mkdir release

:: build source code package
call build-src-package.cmd || goto build_failed

:: build all binaries
call build.cmd --all --use-winsdk || goto build_failed
copy .\bin\ultradefrag-%UDVERSION_SUFFIX%.bin.i386.exe .\release\
copy .\bin\amd64\ultradefrag-%UDVERSION_SUFFIX%.bin.amd64.exe .\release\
copy .\bin\ia64\ultradefrag-%UDVERSION_SUFFIX%.bin.ia64.exe .\release\
copy .\bin\ultradefrag-portable-%UDVERSION_SUFFIX%.bin.i386.zip .\release\
copy .\bin\amd64\ultradefrag-portable-%UDVERSION_SUFFIX%.bin.amd64.zip .\release\
copy .\bin\ia64\ultradefrag-portable-%UDVERSION_SUFFIX%.bin.ia64.zip .\release\

:: update history file
copy /Y .\HISTORY.TXT ..\..\web\

:: update version notification
set version_string=%ULTRADFGVER%
if "%RELEASE_STAGE%" neq "" set version_string=%ULTRADFGVER% %RELEASE_STAGE%

echo %version_string%> ..\..\web\version.ini
echo %version_string%> ..\..\web\version_xp.ini

if "%RELEASE_STAGE%" equ "" echo %version_string%> ..\..\web\stable-version.ini

:: update documentation
copy /Y ..\doc\handbook\doxy-doc\html\*.* ..\..\web\handbook        || goto build_failed
copy /Y .\dll\udefrag\doxy-doc\html\*.*   ..\..\web\doc\lib\udefrag || goto build_failed
copy /Y .\dll\zenwinx\doxy-doc\html\*.*   ..\..\web\doc\lib\zenwinx || goto build_failed

echo.
echo Release made successfully!
title Release made successfully!
exit /B 0

:build_failed
echo.
echo Release building error!
title Release building error!
exit /B 1
