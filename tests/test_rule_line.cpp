#include "include/database/entities/RouteProfile.h"

#include <QTest>

using Configs::NormalizeRuleLine;
using Configs::SplitRuleLine;

class TestRuleLine : public QObject {
    Q_OBJECT

private slots:
    void emptyMatchersAreRejected();
    void suffixLosesItsLeadingDot();
    void aliasesCollapseToOneKind();
    void decorationIsStripped();
    void nonRulesAreRejected();
    void splitTrimsBothHalves();
    void splitKeepsAWindowsDriveLetter();
    void splitRejectsLinesWithNothingToMatch();
};

// A rule that normalises to a bare kind has nothing to match on, so it would sit in
// the profile as an action with no condition and quietly catch everything.
void TestRuleLine::emptyMatchersAreRejected() {
    QVERIFY(NormalizeRuleLine("suffix:.").isEmpty());
    QVERIFY(NormalizeRuleLine("domain_suffix: .").isEmpty());
    QVERIFY(NormalizeRuleLine(".").isEmpty());
    QVERIFY(NormalizeRuleLine("suffix:").isEmpty());
}

void TestRuleLine::suffixLosesItsLeadingDot() {
    QCOMPARE(NormalizeRuleLine("suffix:.example.com"), QStringLiteral("suffix:example.com"));
    QCOMPARE(NormalizeRuleLine(".example.com"), QStringLiteral("suffix:example.com"));
}

void TestRuleLine::aliasesCollapseToOneKind() {
    QCOMPARE(NormalizeRuleLine("domain_suffix:example.com"), QStringLiteral("suffix:example.com"));
    QCOMPARE(NormalizeRuleLine("full:example.com"), QStringLiteral("domain:example.com"));
    QCOMPARE(NormalizeRuleLine("rule_set:geosite-ru-blocked"), QStringLiteral("ruleset:geosite-ru-blocked"));
}

void TestRuleLine::decorationIsStripped() {
    QCOMPARE(NormalizeRuleLine("  - \"example.com\",  "), QStringLiteral("domain:example.com"));
}

void TestRuleLine::nonRulesAreRejected() {
    QVERIFY(NormalizeRuleLine("# a comment").isEmpty());
    QVERIFY(NormalizeRuleLine("// also a comment").isEmpty());
    QVERIFY(NormalizeRuleLine("   ").isEmpty());
    QVERIFY(NormalizeRuleLine("two words").isEmpty());
}

// A value typed as "ip: 100.64.0.0/10" reads naturally, and the space used to reach
// the core verbatim, where it fails the whole router.
void TestRuleLine::splitTrimsBothHalves() {
    QCOMPARE(SplitRuleLine("ip: 100.64.0.0/10"), std::make_pair(QStringLiteral("ip"), QStringLiteral("100.64.0.0/10")));
    QCOMPARE(SplitRuleLine("  domain : example.com  "), std::make_pair(QStringLiteral("domain"), QStringLiteral("example.com")));
}

void TestRuleLine::splitKeepsAWindowsDriveLetter() {
    QCOMPARE(SplitRuleLine("processPath: C:\\Program Files\\app.exe"),
             std::make_pair(QStringLiteral("processPath"), QStringLiteral("C:\\Program Files\\app.exe")));
}

void TestRuleLine::splitRejectsLinesWithNothingToMatch() {
    QVERIFY(SplitRuleLine("ip:   ").second.isEmpty());
    QVERIFY(SplitRuleLine("no separator").first.isEmpty());
    QVERIFY(SplitRuleLine(":leading").first.isEmpty());
}

QTEST_GUILESS_MAIN(TestRuleLine)
#include "test_rule_line.moc"
