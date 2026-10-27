using System;
using Ac.Core;
using UnityEditor;
using UnityEngine;
namespace Ac.Tests
{
    public static class SelfTestHarnessContract
    {
        private static void MustReject(Action action)
        {
            try { action(); } catch (InvalidOperationException) { return; }
            throw new Exception("harness accepted an invalid operation");
        }
        public static void Verify()
        {
            SelfTest.Reset();
            MustReject(() => SelfTest.RunAll(false));
            SelfTest.Add("contract.one", () => { });
            MustReject(() => SelfTest.Add("contract.one", () => { }));
            MustReject(() => SelfTest.RequireGroup("absent."));
            SelfTest.RequireGroup("contract.");
            SelfTest.Reset();
            SelfTest.Add("contract.frozen", () =>
            {
                MustReject(() => SelfTest.Reset());
                MustReject(() => SelfTest.Add("contract.late", () => { }));
                MustReject(() => SelfTest.RunAll(false));
            });
            if (SelfTest.RunAll(false) != 0) throw new Exception("harness mutation contract failed");
            SelfTest.Reset();
        }
        public static void Run()
        {
            Verify(); Debug.Log("HARNESS CONTRACT OK"); EditorApplication.Exit(0);
        }
    }
}
