@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Merges feature/Serialisation into main, builds with MSVC, runs every test,
rem and pushes main only if everything passes. Written by Claude; see the chat.

set "RES=%~dp0_claude_merge"
set "LOG=%~dp0.git\claude-merge.log"
set "EXPECTED_MAIN=44b5e0d0b3326633920390a53d629a58b2e8679f"
set "EXPECTED_FEATURE=3f2daad9f5b03f219d826fc948ad0d9b93b9ed89"
set "EXPECTED_TREE=8f8b257c21c0a8e13323548a506b95500728f68f"
set "GIT=git"
where git >nul 2>nul || set "GIT=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

echo Fusion de feature/Serialisation dans main > "%LOG%"

echo [1/7] Verification de l'etat du depot...
"%GIT%" diff --quiet HEAD >>"%LOG%" 2>&1 || (set "ERR=Tu as des modifications non commitees : commit-les ou annule-les, puis relance." & goto :fail_nomerge)
"%GIT%" diff --cached --quiet >>"%LOG%" 2>&1 || (set "ERR=Des changements sont indexes : commit-les ou annule-les, puis relance." & goto :fail_nomerge)

echo [2/7] Recuperation de GitHub (fetch)...
"%GIT%" fetch origin >>"%LOG%" 2>&1 || (set "ERR=git fetch a echoue (connexion ou identifiants)." & goto :fail_nomerge)
for /f "delims=" %%r in ('"%GIT%" rev-parse origin/main') do set "MAIN=%%r"
for /f "tokens=1" %%r in ('"%GIT%" for-each-ref --format^=%%^(objectname^) refs/remotes/origin/feature/S*rialisation') do set "FEATURE=%%r"
if /i not "%MAIN%"=="%EXPECTED_MAIN%" (set "ERR=main a change sur GitHub depuis que Claude a prepare la fusion. Redemande a Claude." & goto :fail_nomerge)
if /i not "%FEATURE%"=="%EXPECTED_FEATURE%" (set "ERR=feature/Serialisation a change sur GitHub depuis que Claude a prepare la fusion. Redemande a Claude." & goto :fail_nomerge)

echo [3/7] Passage sur main a jour...
"%GIT%" checkout main >>"%LOG%" 2>&1 || (set "ERR=Impossible de passer sur main." & goto :fail_nomerge)
"%GIT%" merge --ff-only origin/main >>"%LOG%" 2>&1 || (set "ERR=main local a diverge de origin/main." & goto :fail_back)

echo [4/7] Fusion et resolution des conflits...
"%GIT%" merge --no-ff --no-commit %FEATURE% >>"%LOG%" 2>&1
copy /y "%RES%\AssetEngine_AssetEngine.vcxproj" "AssetEngine\AssetEngine.vcxproj" >nul || goto :fail_copy
copy /y "%RES%\AssetEngine_AssetEngine.vcxproj.filters" "AssetEngine\AssetEngine.vcxproj.filters" >nul || goto :fail_copy
copy /y "%RES%\UnitTests_UnitTests.vcxproj" "UnitTests\UnitTests.vcxproj" >nul || goto :fail_copy
copy /y "%RES%\README.md" "README.md" >nul || goto :fail_copy
copy /y "%RES%\UnitTests_SerializationVersioningTests.cpp" "UnitTests\SerializationVersioningTests.cpp" >nul || goto :fail_copy
"%GIT%" checkout --ours -- UnitTests/VersioningTests.cpp >>"%LOG%" 2>&1 || goto :fail_copy
"%GIT%" add -- AssetEngine/AssetEngine.vcxproj AssetEngine/AssetEngine.vcxproj.filters UnitTests/UnitTests.vcxproj README.md UnitTests/SerializationVersioningTests.cpp UnitTests/VersioningTests.cpp >>"%LOG%" 2>&1 || goto :fail_copy
for /f "delims=" %%t in ('"%GIT%" write-tree') do set "TREE=%%t"
if /i not "%TREE%"=="%EXPECTED_TREE%" (set "ERR=Le resultat de la fusion ne correspond pas a celui verifie par Claude. Rien n'a ete pousse." & goto :fail_abort)

echo [5/7] Compilation (Debug x64) avec MSVC...
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -find Common7\IDE\Extensions\TestPlatform\vstest.console.exe`) do set "VSTEST=%%i"
if not defined MSBUILD (set "ERR=MSBuild introuvable." & goto :fail_abort)
"%MSBUILD%" AssetLoader.slnx /m /nologo /v:minimal /p:Configuration=Debug /p:Platform=x64 >>"%LOG%" 2>&1 || (set "ERR=La compilation a echoue. Rien n'a ete pousse." & goto :fail_abort)

echo [6/7] Execution de tous les tests...
if not defined VSTEST (set "ERR=vstest.console introuvable." & goto :fail_abort)
"%VSTEST%" "x64\Debug\UnitTests.dll" >>"%LOG%" 2>&1 || (set "ERR=Des tests echouent. Rien n'a ete pousse." & goto :fail_abort)

echo [7/7] Commit de fusion et push sur main...
"%GIT%" commit -F "%RES%\MERGE_MSG.txt" >>"%LOG%" 2>&1 || (set "ERR=Le commit a echoue." & goto :fail_abort)
"%GIT%" push origin main >>"%LOG%" 2>&1 || (set "ERR=Le push a echoue (droits sur main ?). La fusion est commitee en local sur main." & goto :fail_kept)

echo.
echo TERMINE : feature/Serialisation est fusionnee dans main et poussee sur GitHub.
echo Tu es maintenant sur la branche main.
rmdir /s /q "%RES%" 2>nul
pause
(goto) 2>nul & del "%~f0"

:fail_copy
set "ERR=Impossible d'appliquer la resolution des conflits."
:fail_abort
"%GIT%" merge --abort >>"%LOG%" 2>&1
:fail_back
"%GIT%" checkout - >>"%LOG%" 2>&1
:fail_nomerge
echo.
echo ECHEC : %ERR%
echo Ton depot est revenu comme avant. Details : .git\claude-merge.log
pause
exit /b 1

:fail_kept
echo.
echo ECHEC : %ERR%
echo Details : .git\claude-merge.log
pause
exit /b 1
