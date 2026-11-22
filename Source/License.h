#pragma once
// Ativação do Valved por licença.
// A chave é uma assinatura RSA-2048 (PKCS#1 v1.5 + SHA-256) do texto "VALVED1|<e-mail em minúsculas>".
// Só quem tem a chave privada (fora deste repositório) consegue gerar chaves; o plugin só confere com a chave pública abaixo.
// Arquivo da licença: <dados do app>/Valved/license.xml  (vale para o standalone e para o plugin em qualquer DAW).
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <atomic>
#include <cstring>

namespace vlv
{
class License
{
public:
    static juce::String normaliseEmail (const juce::String& e) { return e.trim().toLowerCase(); }
    static juce::String cleanKey (const juce::String& k) { return k.removeCharacters (" \t\r\n"); }

    static bool verify (const juce::String& email, const juce::String& key)
    {
        const auto em = normaliseEmail (email);
        if (em.isEmpty() || ! em.containsChar ('@')) return false;

        juce::MemoryOutputStream sig;
        if (! juce::Base64::convertFromBase64 (sig, cleanKey (key))) return false;
        constexpr size_t K = 256;
        if (sig.getDataSize() != K) return false;

        // assinatura (big-endian) -> BigInteger (little-endian)
        const auto* p = static_cast<const juce::uint8*> (sig.getData());
        juce::MemoryBlock le (K);
        for (size_t i = 0; i < K; ++i) le[i] = (char) p[K - 1 - i];
        juce::BigInteger s; s.loadFromMemoryBlock (le);

        juce::BigInteger n; n.parseString (modulusHex(), 16);
        if (s.compare (n) >= 0) return false;
        s.exponentModulo (juce::BigInteger (65537), n);

        const auto out = s.toMemoryBlock();
        if (out.getSize() > K) return false;
        juce::uint8 got[K] = {};
        for (size_t i = 0; i < out.getSize(); ++i) got[K - 1 - i] = (juce::uint8) out[i];

        // bloco esperado: 00 01 FF..FF 00 || DigestInfo(SHA-256) || hash
        static const juce::uint8 prefix[] = { 0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20 };
        const juce::String msg = "VALVED1|" + em;
        const auto hash = juce::SHA256 (msg.toUTF8()).getRawData();
        if (hash.getSize() != 32) return false;
        juce::uint8 want[K];
        const size_t tLen = sizeof (prefix) + 32;
        want[0] = 0x00; want[1] = 0x01;
        for (size_t i = 2; i < K - tLen - 1; ++i) want[i] = 0xff;
        want[K - tLen - 1] = 0x00;
        std::memcpy (want + K - tLen, prefix, sizeof (prefix));
        std::memcpy (want + K - 32, hash.getData(), 32);

        juce::uint8 diff = 0;
        for (size_t i = 0; i < K; ++i) diff |= (juce::uint8) (got[i] ^ want[i]);
        return diff == 0;
    }

    static juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Valved").getChildFile ("license.xml");
    }

    // lê o arquivo e confere; devolve true se ativado
    bool reload()
    {
       #if VLV_LICENSE_BYPASS
        active = true; return true;
       #else
        bool ok = false; juce::String em;
        if (auto xml = juce::XmlDocument::parse (file()))
        {
            em = xml->getStringAttribute ("email");
            ok = verify (em, xml->getStringAttribute ("key"));
        }
        { const juce::SpinLock::ScopedLockType sl (lock); licEmail = ok ? normaliseEmail (em) : juce::String(); }
        active = ok;
        return ok;
       #endif
    }

    // confere e, se estiver certo, grava a licença
    bool activate (const juce::String& email, const juce::String& key)
    {
        if (! verify (email, key)) return false;
        juce::XmlElement xml ("VALVEDLICENSE");
        xml.setAttribute ("email", normaliseEmail (email));
        xml.setAttribute ("key", cleanKey (key));
        file().getParentDirectory().createDirectory();
        xml.writeTo (file());
        return reload();
    }

    bool isActive() const noexcept { return active.load (std::memory_order_relaxed); }
    juce::String email() const { const juce::SpinLock::ScopedLockType sl (lock); return licEmail; }

private:
    static const char* modulusHex() { return "B82C4B6670DAD4C84286A9DD969B31CBCBECDAAD7153E0EFFF17696A160FD598F6D45E138E0627D1AFA1E93047EE89F5EB875A71D369514E350559FE05D0AF3509DFFB1AC2E66B65B204A0ADFC8142D0AEA0F56678615FB726D2EA4C2B4A7EA311CB97556FDF80AEF69EA9A2B58E2140D6C3122EBC5C4A99F1C67E6608D33D77E623B1D665C86B36CB1A6AEF0BC1F41627A7096CB0FB9D4B1057105DA5758649AC8286D5CDC6C1A958EF645BBAEB27B9BD1EDFF02601018D8E576FB9CDFB895348D5B932EA761A439DEEA3710DA6F78CDB2D515CAC6F64C809FDF2AE92DCC55FCF7B21F236A2001435B1F4338AC95185E3B2382C9AF0020DD435702DEA69AA51"; }
    std::atomic<bool> active { false };
    mutable juce::SpinLock lock;
    juce::String licEmail;
};
}
