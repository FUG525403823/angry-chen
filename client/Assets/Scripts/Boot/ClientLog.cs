using System;
using System.Collections.Generic;
using Ac.Core;
using UnityEngine;

namespace Ac.Boot
{
    // C15 §5 的运行期接线：`LogSink` 此前**只有用例在用**（装配了但没驱动）—— 出包版一个日志文件都不落，
    // 而运维手册承诺 `logs/client-<yyyyMMdd>.log`（JSON 单行、8 MiB 轮转保留 5 份）与
    // `crash-<yyyyMMdd-HHmmss>.log`。这里把引擎的日志通道接到它上面，根目录固定为
    // `Application.persistentDataPath/logs`（Windows 上就是
    // `%USERPROFILE%\AppData\LocalLow\<公司名>\angry-chen\logs\`）。
    // 只订阅、不拦截：Unity 自己的 Player.log 与 stdout 一律照旧（本类是**追加**一份结构化落盘）。
    public static class ClientLog
    {
        // 同一段异常栈只写一份 crash 文件：出问题的那一帧往往每帧都抛（实跑 player 里就见过每帧一条），
        // 不设上限会把日志目录刷爆，反而看不清第一条。
        public const int MaxCrashFiles = 16;

        private static readonly HashSet<string> CrashSeen = new HashSet<string>();

        public static LogSink Sink { get; private set; }
        public static string RootDirectory { get; private set; }

        // 幂等：编辑器反复 Play（域重载被关掉）时静态字段会残留，重复订阅会让同一行写两遍。
        public static bool Attach(string persistentDataPath)
        {
            if (string.IsNullOrEmpty(persistentDataPath)) return false;
            return AttachRoot(System.IO.Path.Combine(persistentDataPath, "logs"));
        }

        // 直接给"日志根目录"：运行期走上面的 `Attach`，无头用例用它换到临时目录做隔离。
        public static bool AttachRoot(string logsRoot)
        {
            if (Sink != null) return true;
            if (string.IsNullOrEmpty(logsRoot)) return false;
            RootDirectory = logsRoot;
            try
            {
                Sink = new LogSink(RootDirectory);
            }
            catch (Exception ex)
            {
                // 落盘失败不许把客户端带崩：没有日志也要能玩。
                Sink = null;
                Debug.LogWarning("Ac.Boot: 日志落盘不可用（" + RootDirectory + "）: " + ex.Message);
                return false;
            }
            Application.logMessageReceived += OnLogMessage;
            return true;
        }

        // 注销订阅并丢掉 sink（域重载与本类的用例需要回到"未装配"这个起点）。
        public static void Detach()
        {
            if (Sink == null) return;
            Application.logMessageReceived -= OnLogMessage;
            Sink = null;
            RootDirectory = null;
            CrashSeen.Clear();
        }

        // 引擎日志通道（IL2CPP 下同样回调）：普通行写当前文件，未捕获异常另写 crash 文件。
        public static void OnLogMessage(string condition, string stackTrace, LogType type)
        {
            Handle(condition, stackTrace, type);
        }

        // 与 `OnLogMessage` 同一实现，单独暴露给无头用例（不需要真的往引擎里打一条日志）。
        public static void Handle(string condition, string stackTrace, LogType type)
        {
            var sink = Sink;
            if (sink == null) return;
            var level = LevelName(type);
            sink.Write(level, condition);
            if (type != LogType.Exception && type != LogType.Assert) return;
            if (CrashSeen.Count >= MaxCrashFiles) return;
            var key = level + "|" + condition;
            if (!CrashSeen.Add(key)) return;
            sink.WriteCrash(condition + "\n" + stackTrace);
        }

        // 折叠成小写短名（与日志里的 level 字段口径一致）。
        private static string LevelName(LogType type)
        {
            switch (type)
            {
                case LogType.Error:
                    return "error";
                case LogType.Assert:
                    return "assert";
                case LogType.Warning:
                    return "warning";
                case LogType.Exception:
                    return "exception";
                default:
                    return "log";
            }
        }
    }
}
