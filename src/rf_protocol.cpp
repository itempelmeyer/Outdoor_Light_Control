#include "rf_protocol.h"

namespace RfProtocol
{
    DecodedMessage decode(uint64_t code)
    {
        DecodedMessage result{};

        result.valid = false;

        result.device =
            static_cast<uint16_t>(
                (code >> 8) & 0xFFFF
            );

        result.command =
            static_cast<uint16_t>(
                code & 0xFF
            );

        result.counter = 0;
        result.checkNibble = 0;

        result.circuit = 0;
        result.action = Action::Unknown;

        if (code == CODE_CIRCUIT1_OFF)
        {
            result.valid = true;
            result.circuit = 1;
            result.action = Action::Off;
        }
        else if (code == CODE_CIRCUIT1_ON)
        {
            result.valid = true;
            result.circuit = 1;
            result.action = Action::On;
        }

        return result;
    }

    const char *actionToString(Action action)
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

    uint8_t buildCounterByte(uint8_t counter)
    {
        // No rolling counter in this protocol.
        return counter;
    }

    uint64_t buildFrame(
        uint8_t circuit,
        Action action,
        uint8_t counter
    )
    {
        (void)counter;

        if (circuit != 1)
        {
            return 0;
        }

        switch (action)
        {
            case Action::On:
                return CODE_CIRCUIT1_ON;

            case Action::Off:
                return CODE_CIRCUIT1_OFF;

            default:
                return 0;
        }
    }
}