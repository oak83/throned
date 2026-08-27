#include "include/global/Const.hpp"

#include <QTest>

using Configs::SingBox::LogLevelRank;
using Configs::SingBox::LogLineRank;
using Configs::SingBox::NormalizeLogLevel;

class TestLogLevel : public QObject {
    Q_OBJECT

private slots:
    void warningIsAnAliasOfWarn();
    void unknownLevelFallsBackToInfo();
    void ranksRunFromLeastToMostSevere();
    void singBoxLineCarriesItsLevel();
    void xrayLineCarriesItsLevel();
    void ourOwnLinesCarryNoLevel();
    void aLevelWordInsideTheMessageIsNotTheLevel();
};

void TestLogLevel::warningIsAnAliasOfWarn() {
    QCOMPARE(NormalizeLogLevel("warning"), QStringLiteral("warn"));
    QCOMPARE(NormalizeLogLevel("WARNING"), QStringLiteral("warn"));
    QCOMPARE(LogLevelRank("warning"), LogLevelRank("warn"));
}

void TestLogLevel::unknownLevelFallsBackToInfo() {
    QCOMPARE(NormalizeLogLevel(""), QStringLiteral("info"));
    QCOMPARE(NormalizeLogLevel("verbose"), QStringLiteral("info"));
}

void TestLogLevel::ranksRunFromLeastToMostSevere() {
    QVERIFY(LogLevelRank("trace") < LogLevelRank("debug"));
    QVERIFY(LogLevelRank("debug") < LogLevelRank("info"));
    QVERIFY(LogLevelRank("info") < LogLevelRank("warn"));
    QVERIFY(LogLevelRank("warn") < LogLevelRank("error"));
    QVERIFY(LogLevelRank("error") < LogLevelRank("panic"));
}

void TestLogLevel::singBoxLineCarriesItsLevel() {
    QCOMPARE(LogLineRank("INFO[0001] router: loaded geoip"), LogLevelRank("info"));
    QCOMPARE(LogLineRank("DEBUG[0012] dns: lookup example.com"), LogLevelRank("debug"));
    QCOMPARE(LogLineRank("2026-08-25 12:00:00 WARN dns: dial failed"), LogLevelRank("warn"));
}

void TestLogLevel::xrayLineCarriesItsLevel() {
    QCOMPARE(LogLineRank("2026/08/25 12:00:00 [Info] app/log: started"), LogLevelRank("info"));
    QCOMPARE(LogLineRank("2026/08/25 12:00:00 [Warning] transport: retry"), LogLevelRank("warn"));
}

void TestLogLevel::ourOwnLinesCarryNoLevel() {
    QCOMPARE(LogLineRank("Applied your DNS settings over the profile's own."), -1);
    QCOMPARE(LogLineRank("Downloaded Xray geo asset files."), -1);
}

void TestLogLevel::aLevelWordInsideTheMessageIsNotTheLevel() {
    // Only the head is inspected, so a message mentioning a level does not become one.
    QCOMPARE(LogLineRank("Subscription update finished for the group, no error was reported by the server"), -1);
}

QTEST_GUILESS_MAIN(TestLogLevel)

#include "test_log_level.moc"
