#!/bin/bash
# 清理 pn_dev 进程及其所有子线程
# 用法: ./kill_pn_dev.sh

echo "正在查找 pn_dev 进程..."

# 查找 pn_dev 的主进程 PID（排除 grep 自身）
PIDS=$(ps aux | grep '[p]n_dev' | awk '{print $2}')

if [ -z "$PIDS" ]; then
    echo "未找到 pn_dev 进程"
    exit 0
fi

echo "找到以下进程: $PIDS"

# 先尝试正常终止 (SIGTERM)
echo "发送 SIGTERM 信号..."
for PID in $PIDS; do
    kill -15 $PID 2>/dev/null
done

# 等待 2 秒让进程正常退出
sleep 2

# 检查是否还有残留进程
REMAINING=$(ps aux | grep '[p]n_dev' | awk '{print $2}')

if [ -n "$REMAINING" ]; then
    echo "部分进程未退出，发送 SIGKILL 强制终止..."
    for PID in $REMAINING; do
        kill -9 $PID 2>/dev/null
        # 同时终止该进程的所有子线程
        for TID in $(ls /proc/$PID/task/ 2>/dev/null); do
            kill -9 $TID 2>/dev/null
        done
    done
    sleep 1
fi

# 最终确认
FINAL=$(ps aux | grep '[p]n_dev' | awk '{print $2}')
if [ -z "$FINAL" ]; then
    echo "所有 pn_dev 进程已清理完毕"
else
    echo "警告: 以下进程仍存活: $FINAL"
    exit 1
fi
