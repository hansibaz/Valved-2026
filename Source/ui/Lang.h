#pragma once
// Idioma da interface: inglês (padrão) ou português do Brasil.
// - Textos desenhados no paint():  lang::t ("BASS", "GRAVE")
// - Dicas (tooltips) e textos de botões: ficam em inglês no código e são trocados pelo dicionário
//   em lang::applyTree() (o texto original em inglês fica guardado nas propriedades do componente).
// - Nomes dos amps, dos pedais e dos tipos de delay/reverb não são traduzidos.
// A escolha vale para todas as instâncias do plugin e fica salva em  <dados do app>/Valved/settings.xml.
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace lang
{
    enum Language { English = 0, Portuguese = 1 };

    inline juce::File settingsFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Valved").getChildFile ("settings.xml");
    }

    inline int& currentRef()
    {
        static int lang = [] {
            if (auto xml = juce::XmlDocument::parse (settingsFile()))
                if (xml->hasAttribute ("language")) return xml->getIntAttribute ("language") == 1 ? 1 : 0;
            // primeira vez: segue o idioma do sistema
            return juce::SystemStats::getUserLanguage().startsWithIgnoreCase ("pt") ? 1 : 0;
        }();
        return lang;
    }
    inline int current() { return currentRef(); }
    inline bool pt() { return current() == Portuguese; }

    inline void set (int l)
    {
        currentRef() = l == Portuguese ? Portuguese : English;
        std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (settingsFile()));
        if (xml == nullptr) xml = std::make_unique<juce::XmlElement> ("VALVEDSETTINGS");
        xml->setAttribute ("language", currentRef());
        settingsFile().getParentDirectory().createDirectory();
        xml->writeTo (settingsFile());
    }

    inline juce::String t (const char* en, const char* ptText)
    {
        return pt() ? juce::String (juce::CharPointer_UTF8 (ptText)) : juce::String (en);
    }

    // Dicionário das dicas e dos textos de botões
    inline const std::map<juce::String, juce::String>& dict()
    {
        static const std::map<juce::String, juce::String> d = [] {
            std::map<juce::String, juce::String> m;
            auto add = [&] (const char* en, const char* p) { m[en] = juce::String (juce::CharPointer_UTF8 (p)); };
            // cabeçalho
            add ("PEDALS", "PEDAIS");
            add ("AMP", "AMP");
            add ("CAB", "CAIXA");
            add ("FX", "EFEITOS");
            add ("Tuner", "Afinador");
            add ("TUNER", "AFINADOR");
            add ("User IR", "IR do usuário");
            add ("Low: lighter on the CPU (for simpler computers)", "Baixa: mais leve para o processador (computadores mais simples)");
            add ("Medium: balanced quality and CPU", "Média: equilíbrio entre qualidade e processador");
            add ("High: best possible quality (uses much more CPU)", "Alta: a melhor qualidade possível (usa bem mais processador)");
            add ("English", "Inglês");
            add ("Portuguese (Brazil)", "Português (Brasil)");
            // presets
            add ("Previous preset", "Preset anterior");
            add ("Next preset", "Próximo preset");
            add ("Save the current sound in one of the 50 user slots", "Salvar o som atual em um dos 50 espaços do usuário");
            add ("SAVE", "SALVAR");
            // afinador
            add ("Silence the output while tuning", "Silencia a saída enquanto afina");
            add ("MUTE", "MUDO");
            // amp
            add ("DZ MOD: press for the DZ MOD (purple), release for the REC (red)", "DZ MOD: aperte para o DZ MOD (roxo), solte para o REC (vermelho)");
            add ("POWER: off = amp off (no sound)", "POWER: desligado = amp desligado (sem som)");
            add ("STANDBY: off = tubes warm, no sound", "STANDBY: desligado = válvulas aquecidas, sem som");
            // caixa
            add ("LOAD IR...", "CARREGAR IR...");
            add ("IR: up = your loaded impulse response, down = built-in cabinets", "IR: para cima = o seu IR carregado, para baixo = caixas do plugin");
            add ("Cabinet simulation on/off (off = send the amp without cab, e.g. to an external IR loader)",
                 "Simulação de caixa liga/desliga (desligada = o amp sai sem caixa, por exemplo para um IR loader externo)");
            add ("MD421 bass switch: M = music (flat) .. S = speech (max cut)", "Chave de graves do MD421: M = música (plano) .. S = voz (corte máximo)");
            add ("BASS CUT", "CORTE DE GRAVE");
            add ("PRESENCE", "PRESENÇA");
            // pedais e efeitos
            add ("On / Off", "Liga / Desliga");
            add ("TS808 or TS9 circuit (output stage / op-amp)", "Circuito TS808 ou TS9 (estágio de saída / amplificador operacional)");
            add ("Chorus (INTENSITY) or Vibrato (VIB DEPTH / VIB RATE)", "Chorus (INTENSITY) ou Vibrato (VIB DEPTH / VIB RATE)");
            add ("Drive", "Drive"); add ("Tone", "Timbre"); add ("Level", "Nível"); add ("Gain", "Ganho"); add ("Treble", "Agudo");
            add ("Output", "Saída"); add ("Volume", "Volume"); add ("Intensity", "Intensidade"); add ("Vib Depth", "Profundidade do vibrato");
            add ("Vib Rate", "Velocidade do vibrato");
            add ("Tap tempo", "Tap tempo (bata no ritmo)");
            add ("Sync the delay time to the host tempo", "Sincroniza o tempo do delay com o andamento da DAW");
            add ("Valved 7-band: frequencies chosen for guitar", "Valved 7 bandas: frequências escolhidas para guitarra");
            add ("Mark 5-band: the classic 80 / 240 / 750 / 2200 / 6600 Hz graphic EQ", "Mark 5 bandas: o clássico EQ gráfico de 80 / 240 / 750 / 2200 / 6600 Hz");
            // rótulo do 6º knob do delay / reverb
            add ("GRIT", "SUJEIRA"); add ("AGE", "IDADE"); add ("LOSS", "PERDA"); add ("TIME 2", "TEMPO 2"); add ("WIDTH", "LARGURA");
            add ("PATTERN", "PADRÃO"); add ("SMEAR", "BORRÃO"); add ("RISE", "SUBIDA"); add ("SENS", "SENSIB."); add ("BITS", "BITS");
            add ("SIZE", "TAMANHO"); add ("LOW END", "GRAVES"); add ("DWELL", "INTENSID."); add ("AMOUNT", "QUANTIDADE"); add ("BLOOM", "FLORESCER");
            add ("DIFFUSION", "DIFUSÃO"); add ("GATE", "GATE");
            return m;
        }();
        return d;
    }

    inline juce::String tr (const juce::String& en)
    {
        if (! pt()) return en;
        const auto& d = dict();
        const auto it = d.find (en);
        return it != d.end() ? it->second : en;
    }

    // Traduz dicas e textos de botões de toda a árvore de componentes.
    // Componentes com a propriedade "noTr" ficam de fora (o próprio dono troca o texto).
    inline void applyTree (juce::Component& c)
    {
        auto& props = c.getProperties();
        if (! props.contains ("noTr"))
        {
            if (auto* tc = dynamic_cast<juce::SettableTooltipClient*> (&c))
            {
                if (! props.contains ("enTip")) props.set ("enTip", tc->getTooltip());
                const auto en = props["enTip"].toString();
                if (en.isNotEmpty()) tc->setTooltip (tr (en));
            }
            if (auto* b = dynamic_cast<juce::Button*> (&c))
            {
                if (! props.contains ("enText")) props.set ("enText", b->getButtonText());
                const auto en = props["enText"].toString();
                if (en.isNotEmpty()) b->setButtonText (tr (en));
            }
        }
        for (auto* ch : c.getChildren())
            applyTree (*ch);
    }
}
