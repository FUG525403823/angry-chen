using System;
using UnityEngine;
using UnityEngine.Rendering;

namespace Ac.View
{
    public sealed class TracerRenderer : IDisposable
    {
        public const float WidthM = 0.035f;
        private readonly GameObject _root;
        private readonly Material _material;
        private readonly LineRenderer[] _lines = new LineRenderer[Tracer.Capacity];
        private bool _disposed;

        public int VisibleCount { get; private set; }
        public LineRenderer LineAt(int index) { return _lines[index]; }

        public TracerRenderer(Transform parent)
        {
            _root = new GameObject("Ac.Tracers");
            _root.transform.SetParent(parent, false);
            _material = ArenaMaterials.CreateInstanced("Ac/Tracer", new Color32(255, 213, 96, 255));
            if (_material != null) _material.color = new Color(1f, 0.84f, 0.38f, 1f);
            if (_material != null && _material.HasProperty("_EmissionColor"))
            {
                _material.EnableKeyword("_EMISSION");
                _material.SetColor("_EmissionColor", _material.color * 4f);
            }
            for (var i = 0; i < _lines.Length; i++)
            {
                var obj = new GameObject("Tracer" + i);
                obj.transform.SetParent(_root.transform, false);
                var line = obj.AddComponent<LineRenderer>();
                line.sharedMaterial = _material;
                line.useWorldSpace = true;
                line.positionCount = 2;
                line.startWidth = WidthM;
                line.endWidth = WidthM * 0.5f;
                line.shadowCastingMode = ShadowCastingMode.Off;
                line.receiveShadows = false;
                line.enabled = false;
                _lines[i] = line;
            }
        }

        public void Sync(Tracer tracers, bool visible)
        {
            if (_disposed) return;
            VisibleCount = 0;
            for (var i = 0; i < _lines.Length; i++)
            {
                var segment = tracers.Segments[i];
                var show = visible && segment.Alive && _material != null;
                var line = _lines[i];
                line.enabled = show;
                if (!show) continue;
                var progress = Mathf.Clamp01(segment.AgeMs / Tracer.LifetimeMs);
                line.SetPosition(0, Vector3.Lerp(segment.From, segment.To, progress));
                line.SetPosition(1, segment.To);
                line.startWidth = WidthM * (1f - progress);
                line.endWidth = line.startWidth * 0.5f;
                VisibleCount += 1;
            }
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            VisibleCount = 0;
            if (Application.isPlaying)
            {
                UnityEngine.Object.Destroy(_root);
                UnityEngine.Object.Destroy(_material);
            }
            else
            {
                UnityEngine.Object.DestroyImmediate(_root);
                UnityEngine.Object.DestroyImmediate(_material);
            }
        }
    }
}
