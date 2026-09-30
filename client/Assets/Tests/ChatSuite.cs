using Ac.Net;
using Ac.UI;
using Ac.Core;

namespace Ac.Tests
{
    // C12 §5 的局内聊天：规则类（Chat）的输入缓冲、开关时序，以及 OverlayModel 侧的渲染接线。
    //
    // 背景：`Chat` 的规则与限流早就有用例，但**没有任何输入通路** —— 键位表第 11 条 `chat`（默认 Return）
    // 全仓无消费方，屏幕上也没有聊天行。本组用例盯住这两条新接线：字符进缓冲、回车发送并关闭、
    // Esc 只关不发、以及"开缓冲那一帧的残留换行不许被当成空消息发出去"。
    public static class ChatSuite
    {
        private const int ViewportW = 1920;
        private const int ViewportH = 1080;

        public static void Register()
        {
            SelfTest.Add("chat.input_buffer", ChecksInputBuffer);
            SelfTest.Add("chat.open_and_submit", ChecksOpenAndSubmit);
            SelfTest.Add("chat.frame_input_consumed_once", ChecksFrameInputConsumedOnce);
            SelfTest.Add("chat.escape_closes", ChecksEscapeCloses);
            SelfTest.Add("chat.overlay_lines", ChecksOverlayLines);
        }

        private static void ChecksInputBuffer()
        {
            var chat = new Chat();
            SelfTest.True(!chat.Focused, "构造后不该处于输入态", "开着");
            SelfTest.True(!chat.Apply("abc", false, 16.0), "没焦点时输入不该被接收", "收了");

            var focusEvents = 0;
            chat.OnFocusChanged += focused => { focusEvents += 1; };
            chat.ToggleFocus();
            SelfTest.True(chat.Focused, "ToggleFocus 必须能开", "没开");
            chat.ToggleFocus();
            chat.ToggleFocus();
            SelfTest.Equal(3, (long)focusEvents);          // 开 → 关 → 开，同值不重复通知

            chat.Focus(true);
            chat.Apply("羊", false, 16.0);
            SelfTest.True(chat.BufferingText == "羊", "汉字要进缓冲", chat.BufferingText);
            SelfTest.Equal(1, (long)chat.BufferLength);

            chat.Apply("\b\b", false, 16.0);
            SelfTest.True(chat.BufferingText.Length == 0, "退格删到空", chat.BufferingText);

            var longInput = new string('x', Chat.InputMaxChars + 5);
            chat.Apply(longInput, false, 16.0);
            SelfTest.Equal((long)Chat.InputMaxChars, (long)chat.BufferLength);   // 超出缓冲的部分被丢掉
            SelfTest.True(chat.BufferDroppedCount > 0, "丢弃要计数，不许静默", chat.BufferDroppedCount.ToString());

            chat.Focus(false);
            SelfTest.True(chat.BufferingText.Length == 0, "关掉输入态要清空缓冲", chat.BufferingText);
        }

        private static void ChecksOpenAndSubmit()
        {
            var chat = new Chat();
            chat.Focus(true);
            // 打开缓冲的那一帧，Input.inputString 里已经带上触发它的那个 '\n'：必须吃掉，否则
            // 回车会变成"开 → 立刻发一条空消息 → 关"。
            SelfTest.True(chat.Apply("\n", false, 16.0), "开缓冲那一帧的回车只算打字", "被当成发送了");
            SelfTest.True(chat.Focused, "开缓冲那一帧不该被关掉", "关了");
            SelfTest.Equal(0, (long)chat.SentCount);
            SelfTest.Equal(0, (long)chat.DroppedCount);

            chat.Apply("h", false, 16.0);
            chat.Apply("i", false, 16.0);
            SelfTest.True(chat.BufferingText == "hi", "连续键入要累积", chat.BufferingText);

            chat.Apply("\n", false, 16.0);
            SelfTest.True(!chat.Focused, "回车发送后要关掉输入态", "还开着");
            SelfTest.Equal(1, (long)chat.SentCount);
            SelfTest.True(chat.LineCount == 1, "发出去的要进聊天行", chat.LineCount.ToString());
            SelfTest.True(chat.Lines[0] == "hi", "行内容就是键入的原文", chat.Lines[0]);

            // 500ms 间隔：紧接着再发一条会被限流，且提示留在 LastHint（缓冲照关）
            chat.Focus(true);
            chat.Apply("again", false, 16.0);
            chat.Apply("\n", false, 16.0);
            SelfTest.Equal(1, (long)chat.SentCount);
            SelfTest.True(chat.LastHint == Chat.HintTooFast, "过快要有提示", chat.LastHint);

            // 超过 64 字节按出口截断（不是拒绝），并提示"过长已截断"。
            // 先把时钟推过 500ms 窗口，否则这里量到的是限流、不是截断。
            chat.Tick(600f);
            var big = new string('a', Chat.ChatMaxBytes + 20);
            string hint;
            SelfTest.True(chat.TrySend(big, out hint), "超长文本要照发（出口截断）", "被拒了");
            SelfTest.True(hint == Chat.HintTruncated, "超长要有截断提示", hint);
            SelfTest.True(Chat.Utf8Bytes(chat.Lines[chat.LineCount - 1]) <= Chat.ChatMaxBytes, "行内容不许超过 64 字节", "超了");
        }

        private static void ChecksFrameInputConsumedOnce()
        {
            var chat = new Chat();
            chat.SetVisible(true);
            chat.ApplyInputFrame("\n", false, true, 16.0);
            SelfTest.True(chat.Focused, "聊天键打开输入态", "未打开");
            SelfTest.Equal(0, (long)chat.SentCount);
            SelfTest.Equal(0, (long)chat.DroppedCount);

            chat.ApplyInputFrame("hi", false, false, 16.0);
            chat.ApplyInputFrame("\n", false, true, 16.0);
            SelfTest.True(!chat.Focused, "发送键只消费一次，关闭输入态供驱动恢复游戏输入", "同帧重新打开");
            SelfTest.Equal(1, (long)chat.SentCount);
            SelfTest.True(chat.Lines[0] == "hi", "只发送键入的内容", chat.Lines[0]);
            chat.ApplyInputFrame("", false, false, 16.0);
            SelfTest.True(!chat.Focused, "下一帧保持关闭", "重新打开");
            SelfTest.Equal(1, (long)chat.SentCount);

            chat.ApplyInputFrame("\n", false, true, 16.0);
            chat.ApplyInputFrame("draft", false, false, 16.0);
            chat.ApplyInputFrame("", true, false, 16.0);
            SelfTest.True(!chat.Focused, "Esc 关闭聊天", "未关闭");
            SelfTest.Equal(1, (long)chat.SentCount);
            SelfTest.Equal(0, (long)chat.BufferLength);
        }

        private static void ChecksEscapeCloses()
        {
            var chat = new Chat();
            chat.Focus(true);
            chat.Apply("half", false, 16.0);
            SelfTest.True(!chat.Apply("", true, 16.0), "Esc 那一帧不算打字", "算了");
            SelfTest.True(!chat.Focused, "Esc 要关掉输入态", "还开着");
            SelfTest.Equal(0, (long)chat.SentCount);       // Esc 只关不发
            SelfTest.True(chat.BufferingText.Length == 0, "Esc 要丢掉没发出去的内容", chat.BufferingText);
        }

        private static void ChecksOverlayLines()
        {
            var chat = new Chat();
            var model = new OverlayModel();
            var sources = PlayingSources(chat);
            var baseline = model.Build(sources, ViewportW, ViewportH);

            // 不可见 + 没焦点：一条聊天绘制项都不许产
            chat.Push(0, "隐藏时不画");
            SelfTest.Equal((long)baseline, (long)model.Build(sources, ViewportW, ViewportH));
            SelfTest.True(!HasText(model, "隐藏时不画"), "Visible=false 时不许画聊天行", "画了");

            chat.SetVisible(true);
            SelfTest.True(model.Build(sources, ViewportW, ViewportH) == baseline + 1, "可见后要画出那一行", "没画");
            SelfTest.True(HasText(model, "隐藏时不画"), "聊天行内容要原样上屏", "没找到");

            // 输入态：最下面多一行"说: <缓冲>"，空缓冲也要看得见光标
            chat.Focus(true);
            var withPrompt = model.Build(sources, ViewportW, ViewportH);
            SelfTest.Equal((long)(baseline + 2), (long)withPrompt);
            SelfTest.True(HasText(model, OverlayModel.ChatPromptEmpty), "空缓冲要显示光标行", OverlayModel.ChatPromptEmpty);

            chat.Apply("你好", false, 16.0);
            model.Build(sources, ViewportW, ViewportH);
            SelfTest.True(HasText(model, OverlayModel.ChatPromptPrefix + "你好"), "缓冲区内容要跟着上屏", "没跟上");
            chat.Focus(false);

            // 行数封顶：只画最近 Chat.ChatMaxLines 行（多余的既不上屏也不越界）
            var many = new Chat();
            many.SetVisible(true);
            for (var i = 0; i < Chat.ChatMaxLines + 3; i++) many.Push(0, "行" + i);
            SelfTest.Equal((long)Chat.ChatMaxLines, (long)many.LineCount);

            var manyModel = new OverlayModel();
            manyModel.Build(PlayingSources(many), ViewportW, ViewportH);
            SelfTest.True(HasText(manyModel, "行" + (Chat.ChatMaxLines + 2)), "最后一行必须上屏", "没有");
            SelfTest.True(!HasText(manyModel, "行0"), "最旧那几行要被挤掉", "还在");
            SelfTest.True(manyModel.Count <= OverlayModel.ItemCapacity, "绘制项不许越过固定缓冲", manyModel.Count.ToString());
        }

        // 走生产同一条路：相位从 MatchStatePayload 进 Lobby，OverlayModel 只读结果（不自己推相位）
        private static OverlaySources PlayingSources(Chat chat)
        {
            var lobby = new Lobby();
            var state = default(MatchStatePayload);
            state.Phase = Hud.PhasePlaying;
            state.Wave = 1;
            state.Players = new MatchStatePlayer[0];
            lobby.Apply(state, 1);
            SelfTest.True(lobby.HudVisible, "playing 相位下 HUD 必须可见（用例前提）", "不可见");

            var sources = default(OverlaySources);
            sources.Hud = new Hud();
            sources.Lobby = lobby;
            sources.Chat = chat;
            sources.Players = state.Players;
            return sources;
        }

        private static bool HasText(OverlayModel model, string exact)
        {
            var items = model.Items;
            for (var i = 0; i < model.Count; i++)
            {
                if (items[i].Kind != OverlayItemKind.Text) continue;
                if (string.Equals(items[i].Text, exact, System.StringComparison.Ordinal)) return true;
            }
            return false;
        }
    }
}
