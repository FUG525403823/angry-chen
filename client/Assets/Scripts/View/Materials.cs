using UnityEngine;

namespace Ac.View
{
    // C07 §5(c)：调色板（与冻结表逐值一致）。
    public struct ArtPalette
    {
        public Color32 Grass;
        public Color32 Dirt;
        public Color32 Fence;
        public Color32 BarnWall;
        public Color32 BarnRoof;
        public Color32 Hay;
        public Color32 Ambient;
        public Color32 Sun;

        public static ArtPalette Default()
        {
            var palette = default(ArtPalette);
            palette.Grass = Hex(0x6F9C52);
            palette.Dirt = Hex(0x8A7250);
            palette.Fence = Hex(0xA9835A);
            palette.BarnWall = Hex(0xB45C4A);
            palette.BarnRoof = Hex(0x59413C);
            palette.Hay = Hex(0xD7C273);
            palette.Ambient = Hex(0x9DC4E6);
            palette.Sun = Hex(0xFFF4E0);
            return palette;
        }

        public static Color32 Hex(int rgb)
        {
            Color32 color = default(Color32);
            color.r = (byte)((rgb >> 16) & 0xFF);
            color.g = (byte)((rgb >> 8) & 0xFF);
            color.b = (byte)(rgb & 0xFF);
            color.a = 255;
            return color;
        }
    }

    // §9：材质表。6 种 URP Lit 材质 + 3 张代码生成贴图，仓内不留任何图片文件。
    public sealed class Materials
    {
        public const int MaterialCount = 6;
        public const int GrassTextureSize = 256;
        public const int DirtTextureSize = 256;
        public const int WoodTextureSize = 128;
        private const uint NoiseSeed = 0x9E3779B9u;

        public ArtPalette Palette { get; private set; }

        private readonly Material[] _materials = new Material[MaterialCount];
        private readonly float[] _roughness = new float[MaterialCount];
        private readonly float[] _metallic = new float[MaterialCount];

        private readonly Texture2D[] _textures = new Texture2D[MaterialCount];

        public Materials()
        {
            Palette = ArtPalette.Default();
            _textures[0] = CreateTexture(GrassTextureSize, Palette.Grass, 1);
            _textures[1] = CreateTexture(DirtTextureSize, Palette.Dirt, 3);
            _textures[2] = CreateTexture(WoodTextureSize, Palette.Fence, 7);
        }

        public void SetPalette(in ArtPalette palette) { Palette = palette; ApplyPalette(); }

        // 用当前调色板创建 6 种材质；URP Lit 取不到时退到可用的着色器，两者都没有就把材质留空，
        // 参数表照样可断言（批处理里着色器可能不可用，数值不该因此丢）。
        public MaterialTable Create()
        {
            ApplyPalette();
            var table = default(MaterialTable);
            table.Grass = _materials[0];
            table.Dirt = _materials[1];
            table.Fence = _materials[2];
            table.BarnWall = _materials[3];
            table.BarnRoof = _materials[4];
            table.Hay = _materials[5];
            return table;
        }

        public float Roughness(int index) { return _roughness[index]; }
        public float Metallic(int index) { return _metallic[index]; }

        // §5(c) 的贴图尺寸与"代码生成"要求：确定性整数噪声，同种子两次逐像素一致。
        public Texture2D[] CreateTextures() { return new[] { _textures[0], _textures[1], _textures[2] }; }

        private static Texture2D CreateTexture(int size, Color32 baseColor, uint salt)
        {
            var texture = new Texture2D(size, size, TextureFormat.RGBA32, false);
            var pixels = new Color32[size * size];
            for (var y = 0; y < size; y++)
            {
                for (var x = 0; x < size; x++)
                {
                    var noise = ArenaMesh.Hash((uint)(x * 73856093) ^ (uint)(y * 19349663) ^ (NoiseSeed + salt));   // 与地形共用同一个哈希原语
                    var shade = (int)(noise % 21) - 10;   // ±10/255 的颗粒，不引入超越函数
                    pixels[y * size + x] = Shift(baseColor, shade);
                }
            }
            texture.SetPixels32(pixels);
            texture.Apply(false, false);
            return texture;
        }

        private static Color32 Shift(Color32 color, int delta)
        {
            var result = color;
            result.r = Clamp(color.r + delta);
            result.g = Clamp(color.g + delta);
            result.b = Clamp(color.b + delta);
            result.a = 255;
            return result;
        }

        private static byte Clamp(int value)
        {
            if (value < 0) return 0;
            if (value > 255) return 255;
            return (byte)value;
        }

        private void Bind(int index, string name, float roughness, float metallic, Color32 color)
        {
            _roughness[index] = roughness;
            _metallic[index] = metallic;
            var shader = Shader.Find("Universal Render Pipeline/Lit");
            if (shader == null) shader = Shader.Find("Standard");
            if (shader == null) return;
            var material = new Material(shader);
            material.name = name;
            material.color = color;
            // §5(c)：贴图挂到材质；铺贴单元写进网格 UV（见 ArenaParams 的 TileM 常量），这里不缩放
            var texture = _textures[index];
            if (texture != null)
            {
                material.mainTexture = texture;
                if (material.HasProperty("_BaseMap")) material.SetTexture("_BaseMap", texture);
                material.mainTextureScale = Vector2.one;
            }
            if (material.HasProperty("_BaseColor")) material.SetColor("_BaseColor", color);
            if (material.HasProperty("_Smoothness")) material.SetFloat("_Smoothness", 1.0f - roughness);
            if (material.HasProperty("_Glossiness")) material.SetFloat("_Glossiness", 1.0f - roughness);
            if (material.HasProperty("_Metallic")) material.SetFloat("_Metallic", metallic);
            _materials[index] = material;
        }

        // §5(c) 的六行参数表只写在这里（名字/粗糙度/金属度），颜色来自当前调色板
        private static readonly string[] Names = { "Arena/Grass", "Arena/Dirt", "Arena/Fence", "Arena/BarnWall", "Arena/BarnRoof", "Arena/Hay" };
        private static readonly float[] RoughnessTable = { 0.92f, 0.95f, 0.85f, 0.80f, 0.70f, 0.90f };
        private static readonly float[] MetallicTable = { 0.0f, 0.0f, 0.0f, 0.0f, 0.05f, 0.0f };

        private void ApplyPalette()
        {
            for (var i = 0; i < MaterialCount; i++) Bind(i, Names[i], RoughnessTable[i], MetallicTable[i], ColorOf(i));
        }

        private Color32 ColorOf(int index)
        {
            if (index == 0) return Palette.Grass;
            if (index == 1) return Palette.Dirt;
            if (index == 2) return Palette.Fence;
            if (index == 3) return Palette.BarnWall;
            if (index == 4) return Palette.BarnRoof;
            return Palette.Hay;
        }
    }

    public struct MaterialTable
    {
        public Material Grass;
        public Material Dirt;
        public Material Fence;
        public Material BarnWall;
        public Material BarnRoof;
        public Material Hay;
    }
}
