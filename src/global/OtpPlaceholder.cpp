#include "include/global/OtpPlaceholder.hpp"

#include "include/database/DatabaseManager.h"
#include "include/database/OtpProfilesRepo.h"

namespace Configs {
    QString ResolveOtpCode(int otpProfileId)
    {
        if (otpProfileId < 0 || dataManager == nullptr || dataManager->otpProfilesRepo == nullptr) return {};
        auto profile = dataManager->otpProfilesRepo->GetOtpProfile(otpProfileId);
        if (profile == nullptr || !profile->Validate().isEmpty()) return {};
        // RFC 4226: the code is the one for the counter as it stands, and only then
        // does the counter move on. Incrementing first shifted every code by one step,
        // so an imported counter=0 already answered with HOTP(K,1).
        const auto code = profile->CurrentCode();
        // HOTP has no time window, so every config build still consumes one step.
        if (profile->type == OTP::Type::HOTP) {
            profile->counter++;
            dataManager->otpProfilesRepo->Save(profile);
        }
        return code;
    }

    QString SubstituteOtp(const QString &text, const QString &code)
    {
        if (code.isEmpty() || text.isEmpty()) return text;
        return QString(text).replace(QString::fromLatin1(kOtpPlaceholder), code);
    }
}
