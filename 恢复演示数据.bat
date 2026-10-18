@echo off
chcp 65001 >nul
rem 恢复演示数据：把 data_demo 中的 6 个演示数据文件（含私信和群消息）复制回 data（会覆盖 data 中的同名文件）
rem data_demo 是演示数据母版，请不要修改它；聊天记录 chat_log.txt 不受影响
cd /d "%~dp0"
if not exist "data_demo\accounts.txt" (
    echo 找不到 data_demo 目录，无法恢复。
    pause
    exit /b 1
)
echo 将用演示数据覆盖 IMSystemYD\data 中的 6 个数据文件（含私信和群消息）（请先关闭正在运行的程序）。
set /p answer=确定恢复吗？(y/n)：
if /i not "%answer%"=="y" (
    echo 已取消。
    pause
    exit /b 0
)
if not exist "data" mkdir "data"
for %%f in (users.txt accounts.txt friends.txt groups.txt subgroups.txt messages.txt) do copy /y "data_demo\%%f" "data\%%f" >nul
echo 演示数据已恢复。预置账号的密码都是 123456。
pause