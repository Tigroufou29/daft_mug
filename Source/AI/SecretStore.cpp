#include "SecretStore.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <wincred.h>
#elif JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
 #include <Security/Security.h>
#endif

namespace dm::ai
{
juce::String MemorySecretStore::description() const
{
    return dm::U ("Aucun coffre sécurisé du système n'est disponible sur cette plateforme : la clé reste uniquement en mémoire pendant la session "
                  "et devra être saisie à nouveau au prochain démarrage.");
}

#if JUCE_WINDOWS
namespace
{
class WindowsCredentialStore : public SecretStore
{
public:
    juce::Result save (const juce::String& secret) override
    {
        const auto utf8 = secret.toUTF8();
        const auto size = (DWORD) secret.getNumBytesAsUTF8();
        CREDENTIALW cred {};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = const_cast<LPWSTR> (target());
        cred.CredentialBlobSize = size;
        cred.CredentialBlob = (LPBYTE) utf8.getAddress();
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE;     // stays on this computer, tied to the Windows user account
        cred.UserName = const_cast<LPWSTR> (L"DaftMug");
        const BOOL ok = CredWriteW (&cred, 0);
        return ok ? juce::Result::ok() : juce::Result::fail (dm::U ("Le gestionnaire d'informations d'identification Windows a refusé l'enregistrement de la clé."));
    }

    juce::String load() override
    {
        PCREDENTIALW cred = nullptr;
        if (! CredReadW (target(), CRED_TYPE_GENERIC, 0, &cred) || cred == nullptr) return {};
        juce::String s = juce::String::fromUTF8 ((const char*) cred->CredentialBlob, (int) cred->CredentialBlobSize);
        CredFree (cred);
        return s;
    }

    juce::Result clear() override
    {
        if (CredDeleteW (target(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND) return juce::Result::ok();
        return juce::Result::fail (dm::U ("Impossible d'effacer la clé du gestionnaire d'informations d'identification Windows."));
    }

    bool isPersistent() const override { return true; }
    juce::String description() const override
    {
        return dm::U ("La clé est conservée dans le gestionnaire d'informations d'identification Windows, lié à votre compte Windows. "
                      "Tout programme exécuté sous votre session peut en principe demander à y accéder : ce n'est pas une protection absolue.");
    }

private:
    static const wchar_t* target() { return L"DaftMug/GeminiApiKey"; }
};
}
#endif

#if JUCE_MAC
namespace
{
// macOS Keychain (generic password, this device only, only while the Mac is unlocked). NOT compiled or tested yet.
class MacKeychainStore : public SecretStore
{
public:
    juce::Result save (const juce::String& secret) override
    {
        remove();
        const auto utf8 = secret.toUTF8();
        Ref data (CFDataCreate (nullptr, (const UInt8*) utf8.getAddress(), (CFIndex) secret.getNumBytesAsUTF8()));
        Dict query = baseQuery();
        CFDictionarySetValue (query.d, kSecValueData, data.r);
        CFDictionarySetValue (query.d, kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
        const auto status = SecItemAdd (query.d, nullptr);
        return status == errSecSuccess ? juce::Result::ok()
                                       : juce::Result::fail (dm::U ("Le Trousseau macOS a refusé l'enregistrement de la clé."));
    }

    juce::String load() override
    {
        Dict query = baseQuery();
        CFDictionarySetValue (query.d, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue (query.d, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef out = nullptr;
        if (SecItemCopyMatching (query.d, &out) != errSecSuccess || out == nullptr) return {};
        Ref data (out);
        return juce::String::fromUTF8 ((const char*) CFDataGetBytePtr ((CFDataRef) data.r), (int) CFDataGetLength ((CFDataRef) data.r));
    }

    juce::Result clear() override
    {
        const auto status = remove();
        return (status == errSecSuccess || status == errSecItemNotFound) ? juce::Result::ok()
                                                                         : juce::Result::fail (dm::U ("Impossible d'effacer la clé du Trousseau macOS."));
    }

    bool isPersistent() const override { return true; }
    juce::String description() const override
    {
        return dm::U ("La clé est conservée dans le Trousseau macOS, sur cet ordinateur uniquement. Tout programme exécuté sous votre session peut "
                      "en principe demander à y accéder (macOS peut alors vous le signaler) : ce n'est pas une protection absolue.");
    }

private:
    struct Ref { CFTypeRef r; explicit Ref (CFTypeRef x) : r (x) {} ~Ref() { if (r != nullptr) CFRelease (r); } Ref (const Ref&) = delete; Ref& operator= (const Ref&) = delete; };
    struct Dict
    {
        CFMutableDictionaryRef d = CFDictionaryCreateMutable (nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        ~Dict() { if (d != nullptr) CFRelease (d); }
        Dict() = default; Dict (const Dict&) = delete; Dict& operator= (const Dict&) = delete;
        Dict (Dict&& o) noexcept : d (o.d) { o.d = nullptr; }
    };
    static Dict baseQuery()
    {
        Dict q;
        Ref service (CFStringCreateWithCString (nullptr, "DaftMug", kCFStringEncodingUTF8));
        Ref account (CFStringCreateWithCString (nullptr, "GeminiApiKey", kCFStringEncodingUTF8));
        CFDictionarySetValue (q.d, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue (q.d, kSecAttrService, service.r);
        CFDictionarySetValue (q.d, kSecAttrAccount, account.r);
        return q;
    }
    static OSStatus remove()
    {
        Dict q = baseQuery();
        return SecItemDelete (q.d);
    }
};
}
#endif

std::unique_ptr<SecretStore> createDefaultSecretStore()
{
   #if JUCE_WINDOWS
    return std::make_unique<WindowsCredentialStore>();
   #elif JUCE_MAC
    return std::make_unique<MacKeychainStore>();
   #else
    return std::make_unique<MemorySecretStore>();
   #endif
}
} // namespace dm::ai
