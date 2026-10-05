#include "audio/GraphConnections.h"

std::vector<ChannelConnection> computeChainConnections (const std::vector<NodeChannels>& seq)
{
    std::vector<ChannelConnection> result;

    for (size_t hop = 0; hop + 1 < seq.size(); ++hop)
    {
        const bool mono = seq[hop].numOuts == 1;
        const int n = mono ? seq[hop + 1].numIns : juce::jmin (seq[hop].numOuts, seq[hop + 1].numIns);
        for (int ch = 0; ch < n; ++ch)
            result.push_back ({ seq[hop].id, mono ? 0 : ch, seq[hop + 1].id, ch });
    }

    return result;
}
