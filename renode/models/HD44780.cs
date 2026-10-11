// HD44780 character LCD driven over GPIO in 4-bit mode (write only, no busy flag).
// Inputs: 0..3 = D4..D7, 4 = RS, 5 = E (data is latched on the falling edge of E).
// Line(row) returns the visible text of a row.

using System;
using System.Text;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    public class HD44780 : IGPIOReceiver
    {
        public HD44780(int columns = 8, int rows = 2)
        {
            this.columns = columns;
            this.rows = rows;
            ddram = new byte[0x80];
            Reset();
        }

        public void Reset()
        {
            for(var i = 0; i < ddram.Length; i++)
            {
                ddram[i] = (byte)' ';
            }
            data = 0;
            rs = false;
            e = false;
            fourBit = false;
            highNibblePending = false;
            address = 0;
            cgram = false;
            DisplayOn = false;
        }

        public void OnGPIO(int number, bool value)
        {
            switch(number)
            {
                case 0: case 1: case 2: case 3:
                    data = value ? (byte)(data | (1 << number)) : (byte)(data & ~(1 << number));
                    break;
                case 4:
                    rs = value;
                    break;
                case 5:
                    if(e && !value)
                    {
                        Latch();
                    }
                    e = value;
                    break;
            }
        }

        public string Line(int row)
        {
            if(row < 0 || row >= rows)
            {
                return "";
            }
            var sb = new StringBuilder();
            var start = row * 0x40;
            for(var i = 0; i < columns; i++)
            {
                var c = ddram[start + i];
                sb.Append(c >= 0x20 && c < 0x7F ? (char)c : '?');
            }
            return sb.ToString();
        }

        public string Screen()
        {
            var sb = new StringBuilder();
            for(var r = 0; r < rows; r++)
            {
                sb.Append(Line(r));
                if(r != rows - 1)
                {
                    sb.Append('|');
                }
            }
            return sb.ToString();
        }

        public bool DisplayOn { get; private set; }

        private void Latch()
        {
            if(!fourBit)
            {
                Execute((byte)(data << 4)); // 8-bit interface: D0..D3 are not connected
                return;
            }
            if(!highNibblePending)
            {
                highNibble = data;
                highNibblePending = true;
                return;
            }
            highNibblePending = false;
            Execute((byte)((highNibble << 4) | data));
        }

        private void Execute(byte value)
        {
            if(rs)
            {
                if(!cgram)
                {
                    ddram[address] = value;
                    address = (byte)((address + 1) & 0x7F);
                }
                return;
            }

            if((value & 0x80) != 0)
            {
                address = (byte)(value & 0x7F);
                cgram = false;
            }
            else if((value & 0x40) != 0)
            {
                cgram = true;
            }
            else if((value & 0x20) != 0)
            {
                fourBit = (value & 0x10) == 0;
                highNibblePending = false;
            }
            else if((value & 0x08) != 0)
            {
                DisplayOn = (value & 0x04) != 0;
            }
            else if(value == 0x01)
            {
                for(var i = 0; i < ddram.Length; i++)
                {
                    ddram[i] = (byte)' ';
                }
                address = 0;
                cgram = false;
            }
            else if((value & 0xFE) == 0x02)
            {
                address = 0;
                cgram = false;
            }
        }

        private byte data;
        private bool rs, e;
        private bool fourBit;
        private bool highNibblePending;
        private byte highNibble;
        private byte address;
        private bool cgram;
        private readonly byte[] ddram;
        private readonly int columns;
        private readonly int rows;
    }
}
