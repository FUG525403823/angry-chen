using Ac.Boot;
using UnityEditor;
using UnityEngine;

namespace Ac.Tests
{
    [InitializeOnLoad]
    public static class PlayModeSmoke
    {
        private const string Active = "Ac.P0.PlaySmoke.Active";
        private const string Deadline = "Ac.P0.PlaySmoke.Deadline";
        static PlayModeSmoke()
        {
            if (SessionState.GetBool(Active, false)) EditorApplication.update += Check;
        }
        public static void Run()
        {
            SessionState.SetBool(Active, true);
            SessionState.SetFloat(Deadline, (float)EditorApplication.timeSinceStartup + 120f);
            EditorApplication.update -= Check;
            EditorApplication.update += Check;
            EditorApplication.EnterPlaymode();
        }
        private static void Finish(int code, string message)
        {
            SessionState.SetBool(Active, false);
            EditorApplication.update -= Check;
            if (code == 0) Debug.Log(message); else Debug.LogError(message);
            EditorApplication.Exit(code);
        }
        private static void Check()
        {
            if (EditorApplication.timeSinceStartup > SessionState.GetFloat(Deadline, 0))
            { Finish(1, "PLAYMODE SMOKE FAIL timeout"); return; }
            if (!EditorApplication.isPlaying || GameBootstrap.Loop == null || GameBootstrap.Loop.Frames < 60) return;
            var root = GameObject.Find(GameBootstrap.RootName);
            var layer = GameBootstrap.Presentation;
            if (root == null || layer == null || layer.MainCamera == null || !layer.MainCamera.enabled)
            { Finish(1, "PLAYMODE SMOKE FAIL bootstrap/camera missing"); return; }
            Finish(0, "PLAYMODE SMOKE OK frames=" + GameBootstrap.Loop.Frames);
        }
    }
}
