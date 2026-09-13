#include "rf_state.h"

namespace
{
    // Index 0 is unused.
    // Circuit numbers are 1, 2, and 3.
    RfState::CircuitState circuitStates[4] = {};
}

namespace RfState
{
    void setState(
        uint8_t circuit,
        RfProtocol::Action action,
        Source source
    )
    {
        if (
            circuit < 1 ||
            circuit > 3
        )
        {
            return;
        }

        if (
            action != RfProtocol::Action::On &&
            action != RfProtocol::Action::Off
        )
        {
            return;
        }

        CircuitState &state =
            circuitStates[circuit];

        state.known = true;

        state.isOn =
            action ==
            RfProtocol::Action::On;

        state.source = source;
        state.lastUpdateMs = millis();
    }


    CircuitState getState(
        uint8_t circuit
    )
    {
        if (
            circuit < 1 ||
            circuit > 3
        )
        {
            return {};
        }

        return circuitStates[circuit];
    }


    const char *sourceToString(
        Source source
    )
    {
        switch (source)
        {
            case Source::Rx:
                return "RX";

            case Source::Tx:
                return "TX";

            default:
                return "UNKNOWN";
        }
    }
}