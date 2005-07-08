@echo off
if not .%1==. goto next
if not .%2==. goto next
echo ������뢠�� ���� number_of_patch �� 䠩� filename.ext
echo ���⠪��:
echo     p.bat filename.ext number_of_patch
echo �ਬ��:
echo     p.bat checkver.cpp 1
goto done

:next
if not exist patchs goto next_path
grep "%1.%2.diff" patchs > nul
if not errorlevel 1 goto error

:next_path
rem ��࠭�� �����
rem copy %1 save\%1

rem �ய��稬
rem ���४�� �� AT
rem patch.exe -c %1 diff\%1.%2.diff
settitle "Path [%2]"
patch.exe -c --fuzz=10 --forward --ignore-whitespace %1 diff\%1.%2.diff

rem ��࠭塞 ��� ����
echo %1.%2.diff >> patchs

:next2
goto done

:error
echo ��� ���� 㦥 ������뢠���!
goto done

:done
exit
