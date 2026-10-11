// STM32F0 GPIO port with open-drain support.
// The stock Renode STM32_GPIOPort ignores external levels on output pins, so the 1-Wire line
// (open-drain output, read back through IDR) cannot be emulated with it.
//   Connections[i] - level driven by the MCU (output: ODR bit, open-drain '1' = released; input: pull-up/down)
//   OnGPIO(i, v)   - level driven by an external device (button, sensor)
//   IDR            - resulting pin level (wired-AND for open-drain, pull-up/down when floating)

using System;
using Antmicro.Renode.Core;
using Antmicro.Renode.Peripherals.Bus;

namespace Antmicro.Renode.Peripherals.GPIOPort
{
    public class STM32F0_GPIOPort : BaseGPIOPort, IDoubleWordPeripheral, IWordPeripheral, IBytePeripheral, IKnownSize
    {
        public STM32F0_GPIOPort(IMachine machine, uint modeResetValue = 0, uint pullUpPullDownResetValue = 0) : base(machine, PinCount)
        {
            this.modeResetValue = modeResetValue;
            this.pullResetValue = pullUpPullDownResetValue;
            externalDriven = new bool[PinCount];
            externalLevel = new bool[PinCount];
            Reset();
        }

        public override void Reset()
        {
            base.Reset();
            mode = modeResetValue;
            pull = pullResetValue;
            outputType = 0;
            speed = 0;
            odr = 0;
            afrl = 0;
            afrh = 0;
            Update();
        }

        public override void OnGPIO(int number, bool value)
        {
            if(number < 0 || number >= PinCount)
            {
                return;
            }
            externalDriven[number] = true;
            externalLevel[number] = value;
            Update();
        }

        public uint ReadDoubleWord(long offset)
        {
            switch(offset)
            {
                case 0x00: return mode;
                case 0x04: return outputType;
                case 0x08: return speed;
                case 0x0C: return pull;
                case 0x10: return InputData();
                case 0x14: return odr;
                case 0x20: return afrl;
                case 0x24: return afrh;
                default: return 0;
            }
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            switch(offset)
            {
                case 0x00: mode = value; break;
                case 0x04: outputType = value & 0xFFFF; break;
                case 0x08: speed = value; break;
                case 0x0C: pull = value; break;
                case 0x14: odr = value & 0xFFFF; break;
                case 0x18: odr = (odr & ~(value >> 16)) | (value & 0xFFFF); break;   // set wins over reset
                case 0x20: afrl = value; break;
                case 0x24: afrh = value; break;
                case 0x28: odr &= ~(value & 0xFFFF); break;
                default: return;
            }
            Update();
        }

        public ushort ReadWord(long offset) => (ushort)ReadNarrow(offset, 0xFFFF);
        public void WriteWord(long offset, ushort value) => WriteNarrow(offset, value, 0xFFFF);
        public byte ReadByte(long offset) => (byte)ReadNarrow(offset, 0xFF);
        public void WriteByte(long offset, byte value) => WriteNarrow(offset, value, 0xFF);

        public long Size => 0x400;

        private uint ReadNarrow(long offset, uint mask)
        {
            var shift = (int)(offset & 3) * 8;
            return (ReadDoubleWord(offset & ~3L) >> shift) & mask;
        }

        private void WriteNarrow(long offset, uint value, uint mask)
        {
            var aligned = offset & ~3L;
            var shift = (int)(offset & 3) * 8;
            if(aligned == 0x18 || aligned == 0x28)
            {
                WriteDoubleWord(aligned, (value & mask) << shift);   // write-only set/reset registers
                return;
            }
            var current = ReadDoubleWord(aligned);
            WriteDoubleWord(aligned, (current & ~(mask << shift)) | ((value & mask) << shift));
        }

        private bool IsOutput(int pin) => ((mode >> (2 * pin)) & 3) == 1;
        private bool IsOpenDrain(int pin) => (outputType & (1u << pin)) != 0;
        private bool OdrBit(int pin) => (odr & (1u << pin)) != 0;

        private bool FloatingLevel(int pin)
        {
            var p = (pull >> (2 * pin)) & 3;
            return p == 1; // pull-up -> high, pull-down/none -> low
        }

        private bool Level(int pin)
        {
            var external = externalDriven[pin] ? externalLevel[pin] : FloatingLevel(pin);
            if(IsOutput(pin))
            {
                return IsOpenDrain(pin) ? (OdrBit(pin) && external) : OdrBit(pin);
            }
            return external;
        }

        private uint InputData()
        {
            uint value = 0;
            for(var i = 0; i < PinCount; i++)
            {
                if(Level(i))
                {
                    value |= 1u << i;
                }
            }
            return value;
        }

        private void Update()
        {
            for(var i = 0; i < PinCount; i++)
            {
                State[i] = Level(i);
                var driven = IsOutput(i) ? OdrBit(i) : FloatingLevel(i);
                if(Connections[i].IsSet != driven)
                {
                    Connections[i].Set(driven);
                }
            }
        }

        private uint mode, outputType, speed, pull, odr, afrl, afrh;
        private readonly uint modeResetValue;
        private readonly uint pullResetValue;
        private readonly bool[] externalDriven;
        private readonly bool[] externalLevel;

        private const int PinCount = 16;
    }
}
