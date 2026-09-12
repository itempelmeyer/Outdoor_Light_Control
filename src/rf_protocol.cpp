#include "rf_protocol.h"

namespace RfProtocol
{
    static uint16_t getCommandFor(
        uint8_t circuit,
        Action action
    )
    {
        if (circuit == 1)
        {
            if (action == Action::Off)
                return 0xA5C9;

            if (action == Action::On)
                return 0x9ABE;
        }

        if (circuit == 2)
        {
            if (action == Action::Off)
                return 0xABCF;

            if (action == Action::On)
                return 0x94B8;
        }

        if (circuit == 3)
        {
            if (action == Action::Off)
                return 0xA3C7;

            if (action == Action::On)
                return 0x9CC0;
        }

        return 0x0000;
    }


    DecodedMessage decode(uint64_t code)
    {
        DecodedMessage decoded{};

        decoded.valid = false;
        decoded.circuit = 0;
        decoded.action = Action::Unknown;

        // First 16 bits appear to identify this remote/device family.
        decoded.device =
            static_cast<uint16_t>(
                (code >> 24) & 0xFFFF
            );

        // Middle 16 bits are the command.
        decoded.command =
            static_cast<uint16_t>(
                (code >> 8) & 0xFFFF
            );

        // Final byte contains:
        // high nibble = complement
        // low nibble  = counter
        uint8_t suffix =
            static_cast<uint8_t>(
                code & 0xFF
            );

        decoded.counter =
            suffix & 0x0F;

        decoded.checkNibble =
            (suffix >> 4) & 0x0F;

        bool counterValid =
            decoded.checkNibble ==
            ((~decoded.counter) & 0x0F);

        bool deviceValid =
            decoded.device == 0x1410;

        switch (decoded.command)
        {
            case 0xA5C9:
                decoded.circuit = 1;
                decoded.action = Action::Off;
                break;

            case 0x9ABE:
                decoded.circuit = 1;
                decoded.action = Action::On;
                break;

            case 0xABCF:
                decoded.circuit = 2;
                decoded.action = Action::Off;
                break;

            case 0x94B8:
                decoded.circuit = 2;
                decoded.action = Action::On;
                break;

            case 0xA3C7:
                decoded.circuit = 3;
                decoded.action = Action::Off;
                break;

            case 0x9CC0:
                decoded.circuit = 3;
                decoded.action = Action::On;
                break;

            default:
                decoded.circuit = 0;
                decoded.action = Action::Unknown;
                break;
        }

        bool commandValid =
            decoded.action != Action::Unknown;

        decoded.valid =
            deviceValid &&
            counterValid &&
            commandValid;

        return decoded;
    }


    const char *actionToString(
        Action action
    )
    {
        switch (action)
        {
            case Action::Off:
                return "OFF";

            case Action::On:
                return "ON";

            default:
                return "UNKNOWN";
        }
    }


    uint8_t buildCounterByte(
        uint8_t counter
    )
    {
        counter &= 0x0F;

        return
            ((~counter & 0x0F) << 4) |
            counter;
    }


    uint64_t buildFrame(
        uint8_t circuit,
        Action action,
        uint8_t counter
    )
    {
        uint16_t command =
            getCommandFor(
                circuit,
                action
            );

        if (command == 0x0000)
        {
            return 0;
        }

        uint8_t suffix =
            buildCounterByte(counter);

        uint64_t frame = 0;

        frame |=
            static_cast<uint64_t>(
                0x1410
            ) << 24;

        frame |=
            static_cast<uint64_t>(
                command
            ) << 8;

        frame |= suffix;

        return frame;
    }
}