using System;
using System.Collections.Generic;
using System.IO;
using Ac.View;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

namespace Ac.Editor
{
    // Reproducible visual review of production meshes, without entering a match or saving a scene.
    public static class ModelPreview
    {
        public static void Capture()
        {
            var output = Path.GetFullPath(Path.Combine(Application.dataPath, "../../build/reports/model-polish",
                Argument("-modelPreviewStage", "after")));
            Directory.CreateDirectory(output);
            var resources = new List<UnityEngine.Object>();
            RemotePlayerVisuals chen = null;
            var oldAmbient = RenderSettings.ambientMode;
            var oldColor = RenderSettings.ambientLight;
            var oldFog = RenderSettings.fog;
            try
            {
                EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
                RenderPipelineSetup.Ensure();
                RenderSettings.ambientMode = AmbientMode.Flat;
                RenderSettings.ambientLight = new Color(.55f, .58f, .65f);
                RenderSettings.fog = false;
                var root = new GameObject("ModelPreview"); resources.Add(root);
                var light = new GameObject("Key").AddComponent<Light>(); resources.Add(light.gameObject);
                light.type = LightType.Directional;
                light.intensity = 1.25f;
                light.color = new Color(1f, .94f, .84f);
                light.transform.rotation = Quaternion.Euler(38f, -35f, 0f);
                light.shadows = LightShadows.Soft;
                var camera = new GameObject("PreviewCamera").AddComponent<Camera>(); resources.Add(camera.gameObject);
                camera.backgroundColor = new Color(.11f, .14f, .19f);
                camera.clearFlags = CameraClearFlags.SolidColor;
                camera.orthographic = true;
                camera.nearClipPlane = .01f;
                camera.farClipPlane = 50f;
                camera.GetUniversalAdditionalCameraData().renderPostProcessing = false;
                var sheepMaterial = ArenaMaterials.CreateSheep("PreviewSheep"); resources.Add(sheepMaterial);
                var weaponMaterial = WeaponMesh.CreateMaterial("PreviewWeapon"); resources.Add(weaponMaterial);
                var report = new List<string>();
                for (var kind = 0; kind < 4; kind++)
                {
                    var model = new GameObject("Sheep"); model.transform.SetParent(root.transform);
                    var mesh = SheepMesh.Build((SheepKind)kind); resources.Add(mesh);
                    AddPart(model.transform, mesh, sheepMaterial);
                    var emblemMesh = SheepMesh.BuildEmblem((SheepKind)kind); resources.Add(emblemMesh);
                    var emblemMaterial = ArenaMaterials.CreateInstanced("PreviewEmblem", SheepMesh.EmblemColorPure((SheepKind)kind));
                    resources.Add(emblemMaterial);
                    if (kind == (int)SheepKind.Elite || kind == (int)SheepKind.King)
                    {
                        emblemMaterial.EnableKeyword("_EMISSION");
                        var color = (Color)SheepMesh.EmblemColorPure((SheepKind)kind);
                        var intensity = kind == (int)SheepKind.King ? 1.6f : 1.1f;
                        emblemMaterial.SetColor("_EmissionColor", new Color(color.r * intensity, color.g * intensity, color.b * intensity, 1));
                        emblemMaterial.globalIlluminationFlags = MaterialGlobalIlluminationFlags.RealtimeEmissive;
                    }
                    var emblem = AddPart(model.transform, emblemMesh, emblemMaterial);
                    emblem.transform.localPosition = SheepMesh.EmblemAnchor((SheepKind)kind);
                    emblem.transform.localRotation = Quaternion.Euler(-55f, 0f, 0f);
                    CaptureModel(camera, model, output, "sheep-" + kind, new Vector3(3.4f, 1.7f, 5f));
                    report.Add(mesh.name + ": " + mesh.vertexCount + " vertices, " + mesh.triangles.Length / 3 + " triangles");
                    model.SetActive(false);
                }
                for (var slot = 0; slot < 3; slot++)
                {
                    var model = new GameObject("Weapon"); model.transform.SetParent(root.transform);
                    var body = WeaponMesh.BuildBody(slot); resources.Add(body);
                    var accent = WeaponMesh.BuildAccent(slot); resources.Add(accent);
                    AddPart(model.transform, body, weaponMaterial); AddPart(model.transform, accent, weaponMaterial);
                    CaptureModel(camera, model, output, "weapon-" + slot, new Vector3(4f, 2.3f, 3.3f));
                    CaptureModel(camera, model, output, "weapon-rear-" + slot, new Vector3(-3f, 2f, -4f));
                    CaptureFirstPerson(camera, model, output, "weapon-fps-" + slot);
                    report.Add("Weapon" + slot + ": " + (body.vertexCount + accent.vertexCount) + " vertices, " +
                        (body.triangles.Length + accent.triangles.Length) / 3 + " triangles");
                    model.SetActive(false);
                }
                chen = new RemotePlayerVisuals(root.transform);
                var views = new EntityViews(); var remote = views.Get(2); remote.Visible = true; remote.Kind = 0;
                chen.SyncFrame(views, 1, 16);
                foreach (var part in chen.Root.GetComponentsInChildren<MeshRenderer>()) part.forceRenderingOff = false;
                CaptureModel(camera, chen.Root, output, "chen", new Vector3(3.4f, 1.6f, 5f));
                CaptureModel(camera, chen.Root, output, "chen-rear", new Vector3(-3.4f, 1.6f, -5f));
                report.Add("Chen: " + chen.RendererCount + " instances, " + chen.TriangleCount + " triangles; live instancing groups: " + chen.SubmittedDraws);
                if (ShaderUtil.ShaderHasError(weaponMaterial.shader))
                    throw new InvalidOperationException("Model vertex-color shader has compiler errors; inspect the preview log.");
                File.WriteAllLines(Path.Combine(output, "geometry.txt"), report);
                Debug.Log("[model-preview] saved " + output);
            }
            catch (Exception ex) { Debug.LogException(ex); EditorApplication.Exit(1); return; }
            finally
            {
                chen?.Dispose();
                for (var i = resources.Count - 1; i >= 0; i--) if (resources[i] != null) UnityEngine.Object.DestroyImmediate(resources[i]);
                RenderSettings.ambientMode = oldAmbient; RenderSettings.ambientLight = oldColor; RenderSettings.fog = oldFog;
            }
            EditorApplication.Exit(0);
        }

        private static GameObject AddPart(Transform parent, Mesh mesh, Material material)
        {
            var go = new GameObject(mesh.name); go.transform.SetParent(parent, false);
            go.AddComponent<MeshFilter>().sharedMesh = mesh;
            go.AddComponent<MeshRenderer>().sharedMaterial = material;
            return go;
        }

        private static void CaptureModel(Camera camera, GameObject model, string output, string name, Vector3 direction)
        {
            camera.orthographic = true;
            camera.aspect = 1.2f;
            var renderers = model.GetComponentsInChildren<MeshRenderer>();
            var bounds = renderers[0].bounds;
            foreach (var renderer in renderers) bounds.Encapsulate(renderer.bounds);
            camera.transform.position = bounds.center + direction.normalized * 8f;
            camera.transform.LookAt(bounds.center);
            // Keep all eight bounds corners in frame at either view angle.
            var extent = 0f;
            for (var x = -1; x <= 1; x += 2)
                for (var y = -1; y <= 1; y += 2)
                    for (var z = -1; z <= 1; z += 2)
                    {
                        var corner = camera.transform.InverseTransformPoint(bounds.center + Vector3.Scale(bounds.extents, new Vector3(x, y, z)));
                        extent = Mathf.Max(extent, Mathf.Abs(corner.y), Mathf.Abs(corner.x) / 1.2f);
                    }
            camera.orthographicSize = extent * 1.15f;
            SaveFrame(camera, output, name, 768, 640);
        }

        private static void CaptureFirstPerson(Camera camera, GameObject model, string output, string name)
        {
            camera.orthographic = false;
            camera.fieldOfView = (float)FpsCamera.DefaultFov;
            camera.nearClipPlane = (float)FpsCamera.NearClipMeters;
            camera.transform.SetPositionAndRotation(Vector3.zero, Quaternion.identity);
            // Match PresentationLayer.BuildWeaponView's resting pose on the main camera.
            var anchor = ViewModel.BaseOffset;
            model.transform.SetParent(camera.transform, false);
            model.transform.localPosition = new Vector3(anchor.x * .60f, anchor.y * .62f, anchor.z + .42f);
            model.transform.localRotation = Quaternion.Euler(3f, -13f, 0f);
            model.transform.localScale = Vector3.one * 1.55f;
            SaveFrame(camera, output, name, 1280, 720);
            camera.nearClipPlane = .01f;
        }

        private static void SaveFrame(Camera camera, string output, string name, int width, int height)
        {
            camera.aspect = (float)width / height;
            var target = RenderTexture.GetTemporary(width, height, 24, RenderTextureFormat.ARGB32);
            var oldTarget = RenderTexture.active;
            var pixels = new Texture2D(width, height, TextureFormat.RGB24, false);
            try
            {
                camera.targetTexture = target;
                camera.Render();
                RenderTexture.active = target;
                pixels.ReadPixels(new Rect(0, 0, width, height), 0, 0); pixels.Apply();
                File.WriteAllBytes(Path.Combine(output, name + ".png"), pixels.EncodeToPNG());
            }
            finally
            {
                camera.targetTexture = null; RenderTexture.active = oldTarget;
                RenderTexture.ReleaseTemporary(target); UnityEngine.Object.DestroyImmediate(pixels);
            }
        }

        private static string Argument(string key, string fallback)
        {
            var args = Environment.GetCommandLineArgs();
            for (var i = 0; i + 1 < args.Length; i++) if (args[i] == key) return args[i + 1];
            return fallback;
        }
    }
}
