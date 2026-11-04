using Ac.UI;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.EventSystems;

namespace Ac.Boot
{
    // Dedicated official uGUI surface; no game rules or local rewards live here.
    public sealed class ExpeditionUiRenderer : MonoBehaviour
    {
        private GameLoop _loop;
        private PresentationLayer _presentation;
        private GameObject _root, _reward;
        private Text _status, _deadline, _receipt, _build, _objectiveMarker, _navigation;
        private float _navigationRefresh;
        private readonly Button[] _cards = new Button[3];
        private readonly Text[] _cardText = new Text[3];
        private Button _reroll, _skip, _tool, _enable, _classic, _again;
        private Font _font;
        private int _seenStates = -1, _seconds = -1, _toolSeconds = -1;
        private bool _lastPending;
        public void Bind(GameLoop loop, PresentationLayer presentation)
        {
            _loop = loop;
            _presentation = presentation;
            if (_root == null) Build();
        }
        private void Build()
        {
            _font = Font.CreateDynamicFontFromOSFont(new[] { "Microsoft YaHei", "Arial" }, 20);
            _root = new GameObject("Ac.ExpeditionCanvas", typeof(RectTransform), typeof(Canvas), typeof(CanvasScaler), typeof(GraphicRaycaster));
            _root.transform.SetParent(transform, false);
            var canvas = _root.GetComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvas.sortingOrder = 20;
            var scale = _root.GetComponent<CanvasScaler>();
            scale.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scale.referenceResolution = new Vector2(1280, 720);
            if (EventSystem.current == null)
            {
                var events = new GameObject("Ac.ExpeditionEventSystem", typeof(EventSystem), typeof(StandaloneInputModule));
                events.transform.SetParent(_root.transform, false);
                // Enter belongs to InputSampler, not implicit card submission.
                events.GetComponent<EventSystem>().sendNavigationEvents = false;
            }
            _status = Label(_root.transform, "Objective", 320, 22, 920, 68, 21);
            _navigation = Label(_root.transform, "ObjectiveNavigation", 350, 96, 580, 45, 20);
            _objectiveMarker = Label(_root.transform, "PurifierWorldMarker", 500, 300, 280, 70, 21);
            _objectiveMarker.color = new Color(0.3f, 1f, 0.75f, 1);
            _objectiveMarker.text = "◆ 净化设施 ◆\n防守区域 · 半径6米";
            _build = Label(_root.transform, "Build", 18, 590, 1100, 100, 17);
            _receipt = Label(_root.transform, "Receipt", 330, 540, 850, 40, 20);
            _reward = Panel(_root.transform, "Reward", 150, 150, 980, 380);
            _deadline = Label(_reward.transform, "Deadline", 20, 12, 900, 42, 23);
            for (var i = 0; i < 3; i++)
            {
                var index = i;
                _cards[i] = Action(_reward.transform, "Card" + i, 18 + i * 322, 65, 306, 218, "", () => Choose(index));
                _cardText[i] = _cards[i].GetComponentInChildren<Text>();
                _cardText[i].fontSize = 18;
            }
            _reroll = Action(_reward.transform, "Reroll", 180, 306, 280, 48, "重抽（本局一次）", () => Send(4));
            _skip = Action(_reward.transform, "Skip", 500, 306, 280, 48, "跳过 → 补给", () => Send(3));
            _tool = Action(_root.transform, "HealingPulse", 980, 615, 275, 54, "治疗脉冲", () => Send(5, 1));
            _enable = Action(_root.transform, "EnableExpedition", 900, 110, 350, 48, "开发 / 试玩：启用远征（主机）", () => Send(1));
            _classic = Action(_root.transform, "ReturnClassic", 900, 166, 350, 48, "返回 Classic（主机）", () => Send(6));
            _again = Action(_root.transform, "Again", 490, 450, 300, 54, "再来一局（主机）", () => Send(7));
        }
        private void Choose(int index)
        {
            var model = _loop.Expedition;
            if (!model.CanChoose) return;
            Send(2, model.State.Choices[index]);
        }
        private void Send(byte op, byte choice = 0)
        {
            if (_loop == null || (_presentation != null && _presentation.SettingsInputBlocked)) return;
            _loop.RequestExpedition(op, choice);
            // The action frame remains modal even if an immediate ack closes the offer.
            if (_loop.Sampler != null && op >= 2 && op <= 4) _loop.Sampler.SetModalBlocked(true);
        }
        private void LateUpdate()
        {
            if (_loop == null || _root == null) return;
            var model = _loop.Expedition;
            var state = model.State;
            _presentation.Flow.ApplyExpedition(model.Visible);
            UpdateObjectiveNavigation(model);
            var blocked = _presentation.SettingsInputBlocked;
            var lobby = _presentation.Flow.LobbyVisible;
            var host = _presentation.Flow.Lobby.IsHost;
            _enable.gameObject.SetActive(lobby && !model.Visible);
            _classic.gameObject.SetActive(lobby && model.Visible);
            _enable.interactable = _classic.interactable = host && !model.Pending && !blocked;
            _reward.SetActive(model.RewardVisible);
            _status.gameObject.SetActive(model.Visible);
            _build.gameObject.SetActive(model.Visible);
            _receipt.gameObject.SetActive(model.Visible || model.Pending);
            _tool.gameObject.SetActive(model.Visible && state.Stage == 2);
            _again.gameObject.SetActive(model.Visible && state.Stage == 3);
            _again.interactable = host && !model.Pending && !blocked;
            for (var i = 0; i < 3; i++) _cards[i].interactable = model.CanChoose && !blocked;
            _reroll.interactable = model.CanChoose && state.RerollsLeft > 0 && !blocked;
            _skip.interactable = model.CanChoose && !blocked;
            _tool.interactable = model.Visible && state.Stage == 2 && model.ToolSeconds == 0 && !model.Pending && !blocked;
            if (_seenStates == _loop.ExpeditionStatesApplied && _seconds == model.RemainingSeconds
                && _toolSeconds == model.ToolSeconds && _lastPending == model.Pending) return;
            _seenStates = _loop.ExpeditionStatesApplied; _seconds = model.RemainingSeconds; _toolSeconds = model.ToolSeconds; _lastPending = model.Pending;
            _receipt.text = model.Pending ? "请求处理中…（等待权威回执）" : ExpeditionHudModel.RequestResultText(model.LastResult);
            if (!model.Visible) return;
            _status.text = state.Stage == 3 ? ExpeditionHudModel.ResultText(state.Result)
                : "远征 · 节点 " + state.Node + "/3  |  净化设施 HP " + state.ObjectiveHp + "/1000\n站立进入中心(12,0)半径6m：净化 " + (state.ProgressMs / 1000) + "/180秒  |  战斗剩余 " + ((state.CombatLeftMs + 999) / 1000) + "秒";
            _deadline.text = "安全奖励 · " + _seconds + "秒  |  " + (state.Resolved != 0 ? "已完成，等待队友" : "请选择一张；截止或跳过转补给");
            for (var i = 0; i < 3; i++)
            {
                var id = state.Choices[i];
                _cardText[i].text = ExpeditionHudModel.CardName(id) + "\n\n" + ExpeditionHudModel.CardTerms(id);
            }
            var build = "权威构筑：";
            for (var id = 1; id <= 6; id++) if ((model.LocalCardMask & (1 << (id - 1))) != 0) build += ExpeditionHudModel.CardName(id) + "  ";
            _build.text = build;
            _tool.GetComponentInChildren<Text>().text = _toolSeconds == 0 ? "治疗脉冲 · 可用（8m / 25HP）" : "治疗脉冲 · 冷却 " + _toolSeconds + "秒";
        }
        private void UpdateObjectiveNavigation(ExpeditionHudModel model)
        {
            var camera = _presentation.MainCamera;
            var visible = model.Visible && model.State.Stage == 2 && camera != null;
            _objectiveMarker.gameObject.SetActive(visible);
            _navigation.gameObject.SetActive(visible);
            if (!visible) return;
            var target = new Vector3(12f, 1.2f, 0f);
            var screen = camera.WorldToScreenPoint(target);
            var width = (float)Screen.width;
            var height = (float)Screen.height;
            // Keep an off-screen/behind objective visible at the corresponding edge.
            var x = screen.z > 0 ? screen.x : width - screen.x;
            var y = screen.z > 0 ? screen.y : height - screen.y;
            x = Mathf.Clamp(x, 145, width - 145);
            y = Mathf.Clamp(y, 100, height - 165);
            var rootRect = _root.GetComponent<RectTransform>().rect;
            var rect = _objectiveMarker.rectTransform;
            rect.anchoredPosition = new Vector2(x / width * rootRect.width - 140, -(1f - y / height) * rootRect.height - 35);
            if (Time.unscaledTime < _navigationRefresh) return;
            _navigationRefresh = Time.unscaledTime + 0.2f;
            var position = camera.transform.position;
            var dx = target.x - position.x;
            var dz = target.z - position.z;
            var distance = Mathf.Sqrt(dx * dx + dz * dz);
            var local = camera.transform.InverseTransformDirection(new Vector3(dx, 0, dz));
            var direction = local.z < 0 ? "后方 · 转身" : Mathf.Abs(local.x) < distance * 0.2f ? "前方" : local.x > 0 ? "右前方" : "左前方";
            _navigation.text = distance <= 6f ? "◆ 已进入净化圈 · 站立防守并清除侵入羊 ◆"
                : "◆ 净化设施 " + distance.ToString("0") + "米 · " + direction + " · 进入6米防守圈";
        }
        private GameObject Panel(Transform parent, string name, float x, float y, float w, float h)
        {
            var obj = new GameObject(name, typeof(RectTransform), typeof(Image));
            obj.transform.SetParent(parent, false); Place(obj, x, y, w, h);
            obj.GetComponent<Image>().color = new Color(0.035f, 0.07f, 0.1f, 0.96f);
            return obj;
        }
        private Text Label(Transform parent, string name, float x, float y, float w, float h, int size)
        {
            var obj = new GameObject(name, typeof(RectTransform), typeof(Text));
            obj.transform.SetParent(parent, false); Place(obj, x, y, w, h);
            var text = obj.GetComponent<Text>(); text.font = _font; text.fontSize = size; text.color = Color.white;
            text.alignment = TextAnchor.MiddleCenter; text.raycastTarget = false;
            return text;
        }
        private Button Action(Transform parent, string name, float x, float y, float w, float h, string title, UnityEngine.Events.UnityAction clicked)
        {
            var obj = Panel(parent, name, x, y, w, h);
            obj.GetComponent<Image>().color = new Color(0.12f, 0.24f, 0.29f, 1);
            var button = obj.AddComponent<Button>(); button.targetGraphic = obj.GetComponent<Image>();
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            button.onClick.AddListener(clicked);
            Label(obj.transform, "Text", 8, 4, w - 16, h - 8, 19).text = title;
            return button;
        }
        private static void Place(GameObject obj, float x, float y, float w, float h)
        {
            var rect = obj.GetComponent<RectTransform>(); rect.anchorMin = rect.anchorMax = new Vector2(0, 1);
            rect.pivot = new Vector2(0, 1); rect.anchoredPosition = new Vector2(x, -y); rect.sizeDelta = new Vector2(w, h);
        }
        private void OnDestroy() { if (_font != null) Destroy(_font); }
    }
}
