// 1-Wire line with up to four DS18B20 sensors, driven by GPIO edges.
// Input 0: level driven by the MCU (true = released). Output Line: level pulled by the sensors.
// Slots are decoded from the low pulse length (virtual time): >= 400 us reset, < 30 us '1', otherwise '0'.
//   Connect i / Disconnect i, SetTemperature i <C>, Rom i

using System;
using System.Collections.Generic;
using System.Linq;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Time;

namespace Antmicro.Renode.Peripherals.Sensors
{
    public class DS18B20Bus : IGPIOReceiver
    {
        public DS18B20Bus(IMachine machine)
        {
            this.machine = machine;
            Line = new GPIO();
            sensors = new Sensor[MaxSensors];
            for(var i = 0; i < MaxSensors; i++)
            {
                sensors[i] = new Sensor(MakeRom(i));
            }
            Reset();
        }

        public void Reset()
        {
            masterLevel = true;
            txSlot = false;
            state = State.Idle;
            active = new List<Sensor>();
            Release();
        }

        public void Connect(int index) { sensors[index].Connected = true; sensors[index].PowerOn(); }
        public void Disconnect(int index) { sensors[index].Connected = false; }
        public void SetTemperature(int index, double celsius) { sensors[index].Temperature = celsius; }
        public string Rom(int index) => BitConverter.ToString(sensors[index].Rom);

        public GPIO Line { get; }
        public bool Trace { get; set; }

        public void OnGPIO(int number, bool value)
        {
            if(number != 0 || value == masterLevel)
            {
                return;
            }
            masterLevel = value;
            var now = machine.ElapsedVirtualTime.TimeElapsed;

            if(!value)
            {
                fallTime = now;
                slot++;
                txSlot = IsTransmitting();
                if(txSlot)
                {
                    var bit = NextTxBit();
                    if(!bit)
                    {
                        PullLow();
                        var mySlot = slot;
                        machine.ScheduleAction(TimeInterval.FromMicroseconds(30), _ => { if(slot == mySlot) Release(); });
                    }
                }
                return;
            }

            var lowUs = (now - fallTime).TotalMicroseconds;
            if(Trace) Logger.Log(LogLevel.Info, "1w: low {0:F1}us state {1}", lowUs, state);
            if(lowUs >= 400)
            {
                BusReset();
            }
            else if(!txSlot)
            {
                ReceiveBit(lowUs < 30);
            }
        }

        private void BusReset()
        {
            active = sensors.Where(s => s.Connected).ToList();
            state = active.Count > 0 ? State.RomCommand : State.Idle;
            rxBits = 0;
            rxByte = 0;
            if(active.Count == 0)
            {
                return;
            }
            var mySlot = slot;
            machine.ScheduleAction(TimeInterval.FromMicroseconds(20), _ => { if(slot == mySlot) PullLow(); });
            machine.ScheduleAction(TimeInterval.FromMicroseconds(140), _ => { if(slot == mySlot) Release(); });
        }

        private bool IsTransmitting()
        {
            return state == State.Transmit || (state == State.Search && searchStep < 2);
        }

        private bool NextTxBit()
        {
            if(state == State.Search)
            {
                var bit = active.Count == 0 || active.Select(s => s.RomBit(searchBit) ^ (searchStep == 1)).Aggregate(true, (a, b) => a && b);
                searchStep++;
                return bit;
            }
            if(txIndex >= txBits.Count)
            {
                state = State.Idle;
                return true;
            }
            return txBits[txIndex++];
        }

        private void ReceiveBit(bool bit)
        {
            if(state == State.Search)
            {
                active = active.Where(s => s.RomBit(searchBit) == bit).ToList();
                searchStep = 0;
                if(++searchBit == 64)
                {
                    state = active.Count == 1 ? State.FunctionCommand : State.Idle;
                }
                return;
            }
            if(state == State.Idle)
            {
                return;
            }
            if(bit)
            {
                rxByte |= (byte)(1 << rxBits);
            }
            if(++rxBits == 8)
            {
                var b = rxByte;
                rxBits = 0;
                rxByte = 0;
                ReceiveByte(b);
            }
        }

        private void ReceiveByte(byte b)
        {
            if(Trace) Logger.Log(LogLevel.Info, "1w: byte {0:X2} in state {1}", b, state);
            switch(state)
            {
            case State.RomCommand:
                switch(b)
                {
                case 0xCC: state = State.FunctionCommand; break;
                case 0x55: state = State.MatchRom; rxBuffer.Clear(); break;
                case 0xF0: state = State.Search; searchBit = 0; searchStep = 0; break;
                case 0x33: StartTransmit(active.Count == 1 ? active[0].Rom : new byte[8]); break;
                default: state = State.Idle; break;
                }
                break;
            case State.MatchRom:
                rxBuffer.Add(b);
                if(rxBuffer.Count == 8)
                {
                    active = active.Where(s => s.Rom.SequenceEqual(rxBuffer)).ToList();
                    state = active.Count > 0 ? State.FunctionCommand : State.Idle;
                }
                break;
            case State.FunctionCommand:
                switch(b)
                {
                case 0x44: foreach(var s in active) s.Convert(); state = State.Idle; break;
                case 0xBE: StartTransmit(active.Count == 1 ? active[0].Scratchpad() : WiredAnd(active.Select(s => s.Scratchpad()))); break;
                case 0x4E: state = State.WriteScratchpad; rxBuffer.Clear(); break;
                default: state = State.Idle; break;
                }
                break;
            case State.WriteScratchpad:
                rxBuffer.Add(b);
                if(rxBuffer.Count == 3)
                {
                    foreach(var s in active) s.Write(rxBuffer[0], rxBuffer[1], rxBuffer[2]);
                    state = State.Idle;
                }
                break;
            }
        }

        private static byte[] WiredAnd(IEnumerable<byte[]> buffers)
        {
            var result = Enumerable.Repeat((byte)0xFF, 9).ToArray();
            foreach(var buf in buffers)
            {
                for(var i = 0; i < 9; i++) result[i] &= buf[i];
            }
            return result;
        }

        private void StartTransmit(byte[] bytes)
        {
            txBits.Clear();
            foreach(var b in bytes)
            {
                for(var i = 0; i < 8; i++) txBits.Add((b & (1 << i)) != 0);
            }
            txIndex = 0;
            state = State.Transmit;
        }

        private void PullLow() => Line.Set(false);
        private void Release() => Line.Set(true);

        private static byte[] MakeRom(int index)
        {
            var rom = new byte[] { 0x28, (byte)(0x10 + index), 0xA5, 0x5A, 0x00, 0x00, (byte)index, 0 };
            rom[7] = Crc8(rom, 7);
            return rom;
        }

        internal static byte Crc8(byte[] data, int length)
        {
            byte crc = 0;
            for(var i = 0; i < length; i++)
            {
                var inbyte = data[i];
                for(var j = 0; j < 8; j++)
                {
                    var mix = (byte)((crc ^ inbyte) & 0x01);
                    crc >>= 1;
                    if(mix != 0) crc ^= 0x8C;
                    inbyte >>= 1;
                }
            }
            return crc;
        }

        private readonly IMachine machine;
        private readonly Sensor[] sensors;
        private List<Sensor> active;
        private readonly List<byte> rxBuffer = new List<byte>();
        private readonly List<bool> txBits = new List<bool>();
        private int txIndex;
        private bool masterLevel;
        private bool txSlot;
        private TimeInterval fallTime;
        private ulong slot;
        private State state;
        private int rxBits;
        private byte rxByte;
        private int searchBit;
        private int searchStep;

        private const int MaxSensors = 4;

        private enum State { Idle, RomCommand, MatchRom, Search, FunctionCommand, WriteScratchpad, Transmit }

        private class Sensor
        {
            public Sensor(byte[] rom) { Rom = rom; PowerOn(); }

            public byte[] Rom { get; }
            public bool Connected { get; set; }
            public double Temperature { get; set; } = 25.0;

            public bool RomBit(int bit) => (Rom[bit >> 3] & (1 << (bit & 7))) != 0;

            public void PowerOn()
            {
                raw = 0x0550; // +85.0 C until the first conversion
                th = 0x4B;
                tl = 0x46;
                config = 0x7F;
            }

            public void Convert()
            {
                var bits = 9 + ((config >> 5) & 3);
                var value = (int)Math.Round(Temperature * 16.0);
                value &= ~((1 << (12 - bits)) - 1);
                raw = (short)value;
            }

            public void Write(byte newTh, byte newTl, byte newConfig)
            {
                th = newTh;
                tl = newTl;
                config = (byte)((newConfig & 0x60) | 0x1F);
            }

            public byte[] Scratchpad()
            {
                var sp = new byte[] { (byte)(raw & 0xFF), (byte)((raw >> 8) & 0xFF), th, tl, config, 0xFF, 0x0C, 0x10, 0 };
                sp[8] = Crc8(sp, 8);
                return sp;
            }

            private short raw;
            private byte th, tl, config;
        }
    }
}
