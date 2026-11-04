using Ac.Net;
using Ac.Core;
namespace Ac.Tests
{
    internal static class ExpeditionCodecSuite
    {
        public static void Register()
        {
            SelfTest.Add("expedition.codec_wire", CheckWire);
            SelfTest.Add("expedition.codec_recovery", CheckRecovery);
            SelfTest.Add("expedition.production_version3", CheckProduction);
        }
        private sealed class Socket : IDatagramSocket
        {
            public readonly System.Collections.Generic.Queue<byte[]> Incoming=new System.Collections.Generic.Queue<byte[]>();
            public readonly System.Collections.Generic.List<byte[]> Sent=new System.Collections.Generic.List<byte[]>();
            public bool IsBound { get; private set; }
            public int Port { get { return 0; } }
            public bool HasDatagram { get { return Incoming.Count>0; } }
            public bool Bind(int port) { IsBound=true;return true; }
            public void Connect(string host,int port) { }
            public int Receive(byte[] buffer) { if(Incoming.Count==0)return 0;var b=Incoming.Dequeue();System.Array.Copy(b,buffer,b.Length);return b.Length; }
            public bool Send(byte[] bytes,int length) {var b=new byte[length];System.Array.Copy(bytes,b,length);Sent.Add(b);return true;}
            public void Close() { }
        }
        private static void CheckProduction()
        {
            var socket=new Socket();var transport=new UdpTransport(socket,()=>0);
            SelfTest.True(transport.Connect("mem",0),"connect");transport.Poll(64);
            SelfTest.Equal(3L,(long)socket.Sent[0][0]);SelfTest.Equal(28,socket.Sent[0].Length);
            var header=new PacketHeader {Version=2,Type=PacketType.HelloAck,Flags=PacketFlags.Reliable,Session=7,MsgId=1};
            socket.Incoming.Enqueue(PacketWriter.Build(header,HandshakeCodec.EncodeHelloAck(1,2),0,8));transport.Poll(64);
            SelfTest.Equal(0L,(long)transport.Session);SelfTest.Equal(8L,(long)socket.Sent[socket.Sent.Count-1][1]);
        }
        private static void CheckRecovery()
        {
            var s=new ExpeditionStatePayload { Mode=1, RunId=42, LocalPid=7, PlayerCount=1, OfferId=2, OfferCount=3, Choices=new byte[]{1,4,0} };
            s.Players[0]=new ExpeditionPlayerState { Pid=7, CardMask=33, ToolReadyAtMs=500, TriggerCount=19 };
            var b=ExpeditionCodec.EncodeState(s); SelfTest.Equal(63,b.Length);
            ExpeditionStatePayload d;SelfTest.Equal((long)DecodeFailure.Ok,(long)ExpeditionCodec.DecodeState(b,out d));
            SelfTest.Equal(19L,(long)d.Players[0].TriggerCount);SelfTest.Equal(4L,(long)d.Choices[1]);
            for(var n=0;n<b.Length;n++) { var shortBytes=new byte[n]; System.Array.Copy(b,shortBytes,n);SelfTest.True(ExpeditionCodec.DecodeState(shortBytes,out d)!=DecodeFailure.Ok,"truncated state rejected"); }
            b[54]=0xc0;SelfTest.True(ExpeditionCodec.DecodeState(b,out d)!=DecodeFailure.Ok,"reserved mask rejected");
        }
        private static void CheckWire()
        {
            var bytes = new byte[] { 0x44,0x33,0x22,0x11,0x88,0x77,0x66,0x55,1,0,0,0,2,6,0,0,0,0 };
            ExpeditionRequest request;
            SelfTest.Equal((long)DecodeFailure.Ok, (long)ExpeditionCodec.DecodeRequest(bytes, out request));
            SelfTest.Equal(0x11223344L, (long)request.RunId);
            var encoded = ExpeditionCodec.EncodeRequest(request);
            SelfTest.Equal(bytes.Length, encoded.Length);
            for (var i = 0; i < bytes.Length; i++) SelfTest.Equal((long)bytes[i], (long)encoded[i]);
        }
    }
}
