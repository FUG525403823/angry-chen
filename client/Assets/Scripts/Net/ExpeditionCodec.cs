using System;
namespace Ac.Net
{
    public struct ExpeditionRequest
    {
        public uint RunId, OfferId, RequestId;
        public byte Op, ChoiceId;
        public uint Reserved;
    }
    public struct ExpeditionPlayerState
    {
        public ushort Pid;
        public byte CardMask;
        public uint ToolReadyAtMs, TriggerCount;
    }
    public sealed class ExpeditionStatePayload
    {
        public byte Schema=1, Mode;
        public uint RunId, Revision, ClockMs;
        public byte Stage, Node, Result, LayoutId=1, ObjectiveId=1, ToolId=1;
        public ushort ObjectiveHp;
        public uint ProgressMs, CombatLeftMs, RewardDeadlineMs;
        public ushort LocalPid;
        public uint OfferId;
        public byte OfferCount;
        public byte[] Choices=new byte[3];
        public byte Resolved, RerollsLeft;
        public uint LastRequestId;
        public byte LastResult, PlayerCount;
        public ExpeditionPlayerState[] Players=new ExpeditionPlayerState[4];
    }
    public static class ExpeditionCodec
    {
        private static bool Valid(ExpeditionRequest v)
        {
            return v.RequestId!=0 && v.Reserved==0 && v.Op>=1 && v.Op<=7 &&
                ((v.Op!=1 && v.Op!=6) || (v.RunId==0 && v.OfferId==0)) && (v.Op!=5 || v.OfferId==0) &&
                (v.Op==2 ? v.ChoiceId<=6 : v.ChoiceId==(v.Op==5 ? 1 : 0));
        }
        public static byte[] EncodeRequest(ExpeditionRequest v)
        {
            if(!Valid(v)) throw new ArgumentException("Invalid expedition request");
            var b=new byte[18]; var p=0;
            PacketWriter.PutU32(b,ref p,v.RunId); PacketWriter.PutU32(b,ref p,v.OfferId); PacketWriter.PutU32(b,ref p,v.RequestId);
            b[p++]=v.Op;b[p++]=v.ChoiceId;PacketWriter.PutU32(b,ref p,v.Reserved);return b;
        }
        public static DecodeFailure DecodeRequest(byte[] b,out ExpeditionRequest v)
        {
            v=default(ExpeditionRequest);
            if(b==null || b.Length!=18)return DecodeFailure.BadLength;
            var r=new PacketReader(b);r.TryReadU32(out v.RunId);r.TryReadU32(out v.OfferId);r.TryReadU32(out v.RequestId);
            r.TryReadU8(out v.Op);r.TryReadU8(out v.ChoiceId);r.TryReadU32(out v.Reserved);
            return Valid(v)?DecodeFailure.Ok:DecodeFailure.BadValue;
        }
        private static bool Valid(ExpeditionStatePayload v)
        {
            if(v==null || v.Schema!=1 || v.Mode>1 || v.Stage>3 || v.Node>3 || v.Result>4 || v.LayoutId!=1 || v.ObjectiveId!=1 || v.ToolId!=1 || v.Resolved>1 || v.RerollsLeft>1 || v.LastResult>9 || v.PlayerCount>4 || (v.OfferCount!=0 && v.OfferCount!=3) || v.Choices==null || v.Choices.Length!=3 || v.Players==null || v.Players.Length<v.PlayerCount)return false;
            if((v.OfferCount==0)!=(v.OfferId==0))return false;
            for(var i=0;i<3;i++) {if(v.Choices[i]>6 || (v.OfferCount==0 && v.Choices[i]!=0))return false;for(var j=0;j<i;j++)if(v.Choices[i]!=0 && v.Choices[i]==v.Choices[j])return false;}
            var local=v.LocalPid==0 && v.PlayerCount==0;
            for(var i=0;i<v.PlayerCount;i++) {var a=v.Players[i];if(a.Pid==0 || a.Pid>1024 || (a.CardMask&0xc0)!=0)return false;local|=a.Pid==v.LocalPid;for(var j=0;j<i;j++)if(a.Pid==v.Players[j].Pid)return false;}
            return local;
        }
        private static void U16(byte[] b,ref int p,ushort v) {b[p++]=(byte)v;b[p++]=(byte)(v>>8);}
        public static byte[] EncodeState(ExpeditionStatePayload v)
        {
            if(!Valid(v))throw new ArgumentException("Invalid expedition state");
            var b=new byte[52+11*v.PlayerCount];var p=0;
            b[p++]=v.Schema;b[p++]=v.Mode;PacketWriter.PutU32(b,ref p,v.RunId);PacketWriter.PutU32(b,ref p,v.Revision);PacketWriter.PutU32(b,ref p,v.ClockMs);
            b[p++]=v.Stage;b[p++]=v.Node;b[p++]=v.Result;b[p++]=v.LayoutId;b[p++]=v.ObjectiveId;b[p++]=v.ToolId;U16(b,ref p,v.ObjectiveHp);
            PacketWriter.PutU32(b,ref p,v.ProgressMs);PacketWriter.PutU32(b,ref p,v.CombatLeftMs);PacketWriter.PutU32(b,ref p,v.RewardDeadlineMs);U16(b,ref p,v.LocalPid);PacketWriter.PutU32(b,ref p,v.OfferId);b[p++]=v.OfferCount;
            for(var i=0;i<3;i++)b[p++]=v.Choices[i];b[p++]=v.Resolved;b[p++]=v.RerollsLeft;PacketWriter.PutU32(b,ref p,v.LastRequestId);b[p++]=v.LastResult;b[p++]=v.PlayerCount;
            for(var i=0;i<v.PlayerCount;i++){var a=v.Players[i];U16(b,ref p,a.Pid);b[p++]=a.CardMask;PacketWriter.PutU32(b,ref p,a.ToolReadyAtMs);PacketWriter.PutU32(b,ref p,a.TriggerCount);}return b;
        }
        public static DecodeFailure DecodeState(byte[] b,out ExpeditionStatePayload v)
        {
            v=null;if(b==null || b.Length<52 || b.Length>96)return DecodeFailure.BadLength;
            var a=new ExpeditionStatePayload();var r=new PacketReader(b);
            r.TryReadU8(out a.Schema);r.TryReadU8(out a.Mode);r.TryReadU32(out a.RunId);r.TryReadU32(out a.Revision);r.TryReadU32(out a.ClockMs);
            r.TryReadU8(out a.Stage);r.TryReadU8(out a.Node);r.TryReadU8(out a.Result);r.TryReadU8(out a.LayoutId);r.TryReadU8(out a.ObjectiveId);r.TryReadU8(out a.ToolId);r.TryReadU16(out a.ObjectiveHp);
            r.TryReadU32(out a.ProgressMs);r.TryReadU32(out a.CombatLeftMs);r.TryReadU32(out a.RewardDeadlineMs);r.TryReadU16(out a.LocalPid);r.TryReadU32(out a.OfferId);r.TryReadU8(out a.OfferCount);
            for(var i=0;i<3;i++)r.TryReadU8(out a.Choices[i]);r.TryReadU8(out a.Resolved);r.TryReadU8(out a.RerollsLeft);r.TryReadU32(out a.LastRequestId);r.TryReadU8(out a.LastResult);r.TryReadU8(out a.PlayerCount);
            if(a.PlayerCount>4 || b.Length!=52+11*a.PlayerCount)return DecodeFailure.BadLength;
            for(var i=0;i<a.PlayerCount;i++){r.TryReadU16(out a.Players[i].Pid);r.TryReadU8(out a.Players[i].CardMask);r.TryReadU32(out a.Players[i].ToolReadyAtMs);r.TryReadU32(out a.Players[i].TriggerCount);}
            if(!Valid(a))return DecodeFailure.BadValue;v=a;return DecodeFailure.Ok;
        }
    }
}
