#pragma once

#include <QString>
#include <QList>
#include <QMutex>

#include "include/database/entities/Profile.h"
#include "include/configs/generate.h"

namespace Stats {
    // Aggregate rate accumulator used for the status-bar / traffic-graph
    // numbers (one for all proxied traffic combined, one for direct).
    struct TrafficLooperEntry {
        QString tag;
        double downlink_rate = 0;
        double uplink_rate = 0;
    };

    // Four live rates share one cell of the status strip, so the reading is kept
    // short - one decimal, a single-letter unit - and padded to a constant column
    // count with figure spaces (U+2007, the width of a digit). Without the padding
    // "9.9K" turning into "10.1M" shifted every neighbouring number once a second;
    // without the shortening the whole reading did not fit the cell at all. The
    // exact byte counts live in the traffic graph and the connection list.
    inline QString PaddedRate(double bytesPerSecond) {
        static const QStringList units{QStringLiteral("B"), QStringLiteral("K"), QStringLiteral("M"),
                                       QStringLiteral("G"), QStringLiteral("T")};
        constexpr QChar figureSpace(0x2007);
        double value = bytesPerSecond;
        int unit = 0;
        while (value >= 1024.0 && unit + 1 < units.size()) {
            value /= 1024.0;
            ++unit;
        }
        return QString::number(value, 'f', 1).rightJustified(5, figureSpace) + units.at(unit);
    }

    inline QString DisplaySpeed(const std::shared_ptr<TrafficLooperEntry> &entry) {
        return UNICODE_LRO + QString("%1↑ %2↓").arg(PaddedRate(entry->uplink_rate), PaddedRate(entry->downlink_rate));
    }

    // Runtime view of a TrafficChainGroup: same watchTag + profile list, plus
    // bookkeeping for delta-based rate computation.
    struct TrafficLooperGroup {
        QString watchTag;
        QList<std::shared_ptr<Configs::Profile>> profiles;
        long long last_update = 0;
        double uplink_rate = 0;
        double downlink_rate = 0;
        // Set when the group credited a non-zero delta since the last persist.
        // Auto-selector pools contribute one idle group per unselected member,
        // so persisting only dirty groups keeps that cost proportional to
        // traffic rather than to pool size.
        bool dirty = false;
    };

    class TrafficLooper {
    public:
        bool loop_enabled = false;
        bool looping = false;
        QMutex loop_mutex;

        std::shared_ptr<TrafficLooperEntry> proxy;
        std::shared_ptr<TrafficLooperEntry> direct;

        void UpdateAll();

        void Loop();

        // Persist every active profile's legacy traffic total to disk in one
        // batched transaction. Called on a slow cadence from the loop and once on
        // stop/exit; runs synchronously on the caller's thread (no thread spawn).
        void PersistTraffic();

        void SetChainGroups(const QList<Configs::TrafficChainGroup>& configGroups);

    private:
        QList<TrafficLooperGroup> groups;
        long long direct_last_update = 0;
    };

    extern TrafficLooper *trafficLooper;
} // namespace Stats
