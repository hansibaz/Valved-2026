#include "CabPage.h"
#include "../PluginProcessor.h"
#include "../dsp/CabMics.h"
#include <BinaryData.h>

using namespace ui;

//==============================================================================
void ModernSwitch::Anim::timerCallback()
{
    const float target = s->getToggleState() ? 1.0f : 0.0f;
    s->pos += (target - s->pos) * 0.35f;
    if (std::abs (target - s->pos) < 0.01f) { s->pos = target; stopTimer(); }
    s->repaint();
}

void ModernSwitch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const float target = getToggleState() ? 1.0f : 0.0f;
    if (pos < 0.0f) pos = target;
    if (pos != target && ! anim.isTimerRunning()) { anim.s = this; anim.startTimerHz (60); }

    auto r = getLocalBounds().toFloat().reduced (1.0f);
    // trilho
    const float thick = isVertical ? juce::jmin (r.getWidth(), r.getHeight() * 0.5f) : juce::jmin (r.getHeight(), r.getWidth() * 0.5f);
    auto track = isVertical ? r.withSizeKeepingCentre (thick, r.getHeight()) : r.withSizeKeepingCentre (r.getWidth(), thick);
    const float rad = thick * 0.5f;
    g.setColour (juce::Colour (0xff0a0a0c));
    g.fillRoundedRectangle (track, rad);
    // brilho do LED no trilho (acende conforme a chave sobe)
    if (pos > 0.0f)
    {
        g.setColour (led.withAlpha (0.22f * pos));
        g.fillRoundedRectangle (track, rad);
    }
    g.setColour (highlighted ? juce::Colour (0xff4a4a54) : juce::Colour (0xff2e2e36));
    g.drawRoundedRectangle (track.reduced (0.5f), rad, 1.0f);

    // botão deslizante
    const float kd = thick - 6.0f;
    const float travel = (isVertical ? track.getHeight() : track.getWidth()) - kd - 6.0f;
    juce::Point<float> kc = isVertical ? juce::Point<float> (track.getCentreX(), track.getBottom() - 3.0f - kd * 0.5f - travel * pos)
                                       : juce::Point<float> (track.getX() + 3.0f + kd * 0.5f + travel * pos, track.getCentreY());
    auto knob = juce::Rectangle<float> (kd, kd).withCentre (kc);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (knob.translated (0.0f, 1.5f));
    juce::ColourGradient kg (juce::Colour (0xff5b5c63), knob.getX(), knob.getY(), juce::Colour (0xff24252a), knob.getRight(), knob.getBottom(), false);
    g.setGradientFill (kg);
    g.fillEllipse (knob);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (knob.reduced (0.5f), 1.0f);
    // LED no centro do botão
    const float lr = kd * 0.2f;
    const auto lc = knob.getCentre();
    if (pos > 0.05f)
    {
        g.setColour (led.withAlpha (0.35f * pos));
        g.fillEllipse (lc.x - lr * 2.6f, lc.y - lr * 2.6f, lr * 5.2f, lr * 5.2f);
    }
    g.setColour (juce::Colour (0xff1a1b1e).interpolatedWith (led, pos));
    g.fillEllipse (lc.x - lr, lc.y - lr, lr * 2, lr * 2);
}

//==============================================================================
SpeakerView::SpeakerView (juce::AudioProcessorValueTreeState& s) : state (s)
{
    px = s.getParameter (ParamID::micX);
    pd = s.getParameter (ParamID::micDist);
    using namespace BinaryData;
    cabPhoto = juce::ImageCache::getFromMemory (cab_4x12_jpg, cab_4x12_jpgSize);
    micImg[0][0] = juce::ImageCache::getFromMemory (sm57_on_axis_png, sm57_on_axis_pngSize);
    micImg[0][1] = juce::ImageCache::getFromMemory (sm57_off_axis_png, sm57_off_axis_pngSize);
    micImg[1][0] = juce::ImageCache::getFromMemory (sm7b_on_axis_png, sm7b_on_axis_pngSize);
    micImg[1][1] = juce::ImageCache::getFromMemory (sm7b_off_axis_png, sm7b_off_axis_pngSize);
    micImg[2][0] = juce::ImageCache::getFromMemory (md421_on_axis_png, md421_on_axis_pngSize);
    micImg[2][1] = juce::ImageCache::getFromMemory (md421_off_axis_png, md421_off_axis_pngSize);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

namespace
{
    constexpr float frameT = 30.0f;   // largura da borda de tolex visível
    constexpr float spacing = 2.22f;  // distância entre centros dos falantes (em raios)

    // Um falante 12" visto de frente
    void drawSpeaker (juce::Graphics& g, juce::Point<float> c, float R, int seed)
    {
        auto circle = [&g, c] (float r) { g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2); };
        // sombra no baffle
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        circle (R * 1.02f);
        // cesta metálica
        juce::ColourGradient basket (juce::Colour (0xff3a3b3f), c.x, c.y - R, juce::Colour (0xff111113), c.x, c.y + R, false);
        g.setGradientFill (basket);
        circle (R);
        g.setColour (juce::Colours::black);
        for (int i = 0; i < 8; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f + 0.2f;
            g.fillEllipse (c.x + std::sin (a) * R * 0.94f - 3.0f, c.y - std::cos (a) * R * 0.94f - 3.0f, 6.0f, 6.0f);
        }
        // suspensão
        const float rs = R * 0.88f;
        juce::ColourGradient sur (juce::Colour (0xff2a2a2d), c.x - rs, c.y - rs, juce::Colour (0xff060606), c.x + rs, c.y + rs, false);
        g.setGradientFill (sur);
        circle (rs);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawEllipse (c.x - rs * 0.95f, c.y - rs * 0.95f, rs * 1.9f, rs * 1.9f, 2.0f);
        // cone
        const float rc = R * 0.80f;
        juce::ColourGradient cone (juce::Colour (0xff141415), c.x, c.y, juce::Colour (0xff2c2c2f), c.x, c.y - rc, true);
        cone.addColour (0.55, juce::Colour (0xff1d1d1f));
        g.setGradientFill (cone);
        circle (rc);
        juce::Random rng (seed);
        for (int i = 0; i < 1800; ++i)
        {
            const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            const float rr = std::sqrt (rng.nextFloat()) * rc;
            g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.05f));
            g.fillRect (c.x + std::sin (a) * rr, c.y - std::cos (a) * rr, 1.2f, 1.2f);
        }
        for (int i = 1; i <= 5; ++i)
        {
            const float rr = rc * (0.3f + 0.13f * (float) i);
            g.setColour (juce::Colours::white.withAlpha (0.035f));
            g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, 1.5f);
        }
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.10f), c.x - rc * 0.6f, c.y - rc * 0.6f,
                                    juce::Colours::transparentWhite, c.x + rc * 0.3f, c.y + rc * 0.3f, false);
        g.setGradientFill (sheen);
        circle (rc);
        // calota
        const float rd = rc * 0.27f;
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        circle (rd * 1.08f);
        juce::ColourGradient cap (juce::Colour (0xff4a4a4f), c.x - rd * 0.4f, c.y - rd * 0.5f, juce::Colour (0xff101012), c.x + rd, c.y + rd, true);
        g.setGradientFill (cap);
        circle (rd);
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillEllipse (c.x - rd * 0.55f, c.y - rd * 0.6f, rd * 0.6f, rd * 0.35f);
    }

    // ---------------- Microfones (desenho em escala real, em mm) ----------------
    // Vista lateral, deitado sobre a tela. Eixo x = eixo do microfone (0 = frente/cápsula,
    // positivo = para trás, até o conector). Medidas das fichas técnicas:
    //  SM57: 157 mm, cabeça de 32 mm (malha de aço sobre grade preta), cabo cinza-escuro afinando.
    //  SM7B: ~190 mm, espuma grande (~96 mm), corpo preto cilíndrico, garfo (yoke) com porca lateral.
    //  MD 421-II: 215 mm, ~48 mm, cesta preta com fendas transversais, anel de graves, presilha.
    struct MicPainter
    {
        juce::Graphics& g; bool shadow;

        // perfil simétrico a partir de pares (x, raio), com cantos suavizados
        static juce::Path profile (std::initializer_list<juce::Point<float>> pts, float round = 2.0f)
        {
            juce::Path p;
            std::vector<juce::Point<float>> v (pts);
            p.startNewSubPath (v.front().x, -v.front().y);
            for (size_t i = 1; i < v.size(); ++i) p.lineTo (v[i].x, -v[i].y);
            for (size_t i = v.size(); i-- > 0;) p.lineTo (v[i].x, v[i].y);
            p.closeSubPath();
            return round > 0.0f ? p.createPathWithRoundedCorners (round) : p;
        }
        // sombreamento de cilindro (luz vindo de cima)
        void shade (const juce::Path& p, float r, juce::Colour base, float shine = 0.35f) const
        {
            if (shadow) { g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillPath (p); return; }
            juce::ColourGradient cg (base.darker (1.2f), 0.0f, -r, base.darker (1.0f), 0.0f, r, false);
            cg.addColour (0.10, base.darker (0.25f));
            cg.addColour (0.26, base.brighter (shine));
            cg.addColour (0.36, base.brighter (shine * 0.35f));
            cg.addColour (0.62, base.darker (0.25f));
            cg.addColour (0.88, base.darker (0.7f));
            g.setGradientFill (cg);
            g.fillPath (p);
        }
        void edge (const juce::Path& p, float alpha = 0.10f) const
        {
            if (shadow) return;
            g.setColour (juce::Colours::white.withAlpha (alpha));
            g.strokePath (p, juce::PathStrokeType (0.5f));
        }
        // malha metálica (tela de aço) dentro de um contorno
        void mesh (const juce::Path& clip, float x0, float x1, float r, float cell, float alpha) const
        {
            if (shadow) return;
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (clip);
            g.setColour (juce::Colours::black.withAlpha (alpha));
            for (float x = x0; x < x1; x += cell) g.fillRect (x, -r, cell * 0.42f, 2.0f * r);
            for (float y = -r; y < r; y += cell) g.fillRect (x0, y, x1 - x0, cell * 0.42f);
            g.setColour (juce::Colours::white.withAlpha (alpha * 0.25f));
            for (float x = x0 + cell * 0.5f; x < x1; x += cell) g.fillRect (x, -r, cell * 0.12f, 2.0f * r);
        }
        void fillShadowOnly (const juce::Path& p) const { g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillPath (p); }

        // ---------------------------------------------------------------- Shure SM57
        void sm57() const
        {
            const juce::Colour body (0xff4b4d52), grille (0xff3a3c40), collar (0xff141416), xlr (0xff8a8d93);
            // cabo: 30 mm junto da cabeça afinando para ~23 mm no conector
            auto handle = profile ({ { 44.0f, 15.0f }, { 52.0f, 14.6f }, { 148.0f, 11.6f }, { 152.0f, 11.4f } }, 1.5f);
            shade (handle, 15.0f, body, 0.30f);
            edge (handle);
            // anel do conector XLR
            auto end = profile ({ { 151.0f, 11.4f }, { 157.0f, 11.0f } }, 1.0f);
            shade (end, 11.4f, xlr, 0.55f);
            // gola preta (grade de policarbonato) entre a cabeça e o cabo
            auto col = profile ({ { 38.0f, 16.0f }, { 46.0f, 15.2f } }, 1.0f);
            shade (col, 16.0f, collar, 0.25f);
            // cabeça: malha de aço, frente reta com borda arredondada
            auto head = profile ({ { 0.0f, 14.5f }, { 1.5f, 16.0f }, { 39.0f, 16.0f } }, 3.0f);
            shade (head, 16.0f, grille, 0.55f);
            mesh (head, 0.0f, 39.0f, 16.0f, 1.25f, 0.55f);
            edge (head, 0.14f);
            // aro na frente e a fenda entre malha e gola
            if (! shadow)
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRect (38.0f, -15.8f, 1.2f, 31.6f);
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.fillRect (0.6f, -14.0f, 0.8f, 28.0f);
            }
        }

        // ---------------------------------------------------------------- Shure SM7B
        void sm7b() const
        {
            const juce::Colour body (0xff1e1f22), foam (0xff19191b), yoke (0xff2a2b2f), nut (0xff9fa2a8);
            // garfo (yoke): braço que desce do pino lateral até a base de rosca
            {
                auto arm = profile ({ { 92.0f, 9.0f }, { 132.0f, 9.0f } }, 4.0f).createPathWithRoundedCorners (0.0f);
                juce::Path y; y.addRoundedRectangle (100.0f, 0.0f, 22.0f, 72.0f, 5.0f);
                juce::Path base; base.addRoundedRectangle (93.0f, 64.0f, 36.0f, 18.0f, 4.0f);
                if (shadow) { fillShadowOnly (y); fillShadowOnly (base); }
                else
                {
                    juce::ColourGradient yg (yoke.brighter (0.35f), 100.0f, 0.0f, yoke.darker (0.6f), 122.0f, 0.0f, false);
                    g.setGradientFill (yg); g.fillPath (y); g.fillPath (base);
                    g.setColour (juce::Colours::white.withAlpha (0.08f)); g.strokePath (y, juce::PathStrokeType (0.6f)); g.strokePath (base, juce::PathStrokeType (0.6f));
                }
                juce::ignoreUnused (arm);
            }
            // corpo cilíndrico preto com borda frontal e traseira
            auto bodyP = profile ({ { 50.0f, 33.0f }, { 56.0f, 32.0f }, { 180.0f, 32.0f }, { 186.0f, 30.0f }, { 190.0f, 27.0f } }, 3.0f);
            shade (bodyP, 33.0f, body, 0.40f);
            edge (bodyP, 0.08f);
            if (! shadow)
            {
                // frisos de acabamento do corpo
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillRect (60.0f, -32.0f, 1.0f, 64.0f);
                g.fillRect (176.0f, -32.0f, 1.0f, 64.0f);
            }
            // porca grande do garfo (no centro do corpo)
            {
                const float kr = 17.0f; const juce::Point<float> kc (111.0f, 0.0f);
                juce::Path k; k.addEllipse (kc.x - kr, kc.y - kr, kr * 2, kr * 2);
                if (shadow) fillShadowOnly (k);
                else
                {
                    juce::ColourGradient kg (nut.brighter (0.3f), kc.x - kr, kc.y - kr, nut.darker (1.4f), kc.x + kr, kc.y + kr, false);
                    g.setGradientFill (kg); g.fillPath (k);
                    g.setColour (juce::Colours::black.withAlpha (0.45f));
                    for (int i = 0; i < 24; ++i)
                    {
                        const float a = juce::MathConstants<float>::twoPi * (float) i / 24.0f;
                        g.drawLine (kc.x + std::cos (a) * kr * 0.80f, kc.y + std::sin (a) * kr * 0.80f, kc.x + std::cos (a) * kr, kc.y + std::sin (a) * kr, 0.9f);
                    }
                    juce::ColourGradient cg2 (juce::Colour (0xff3a3b40), kc.x - kr * 0.5f, kc.y - kr * 0.5f, juce::Colour (0xff101012), kc.x + kr * 0.5f, kc.y + kr * 0.5f, false);
                    g.setGradientFill (cg2);
                    g.fillEllipse (kc.x - kr * 0.62f, kc.y - kr * 0.62f, kr * 1.24f, kr * 1.24f);
                    g.setColour (juce::Colours::white.withAlpha (0.25f));
                    g.drawEllipse (kc.x - kr, kc.y - kr, kr * 2, kr * 2, 0.6f);
                }
            }
            // espuma (windscreen): domo grande e arredondado, um pouco mais largo que o corpo
            juce::Path f;
            f.startNewSubPath (58.0f, -46.0f);
            f.cubicTo (30.0f, -50.0f, 0.0f, -40.0f, 0.0f, 0.0f);
            f.cubicTo (0.0f, 40.0f, 30.0f, 50.0f, 58.0f, 46.0f);
            f.cubicTo (62.0f, 30.0f, 62.0f, -30.0f, 58.0f, -46.0f);
            f.closeSubPath();
            if (shadow) fillShadowOnly (f);
            else
            {
                juce::ColourGradient fg (juce::Colour (0xff3b3b40), 18.0f, -30.0f, foam, 40.0f, 30.0f, true);
                fg.addColour (0.65, juce::Colour (0xff1c1c1f));
                g.setGradientFill (fg);
                g.fillPath (f);
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (f);
                juce::Random rng (5);
                for (int i = 0; i < 2600; ++i)
                {
                    g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.09f));
                    g.fillRect (rng.nextFloat() * 62.0f, rng.nextFloat() * 100.0f - 50.0f, 0.8f, 0.8f);
                }
                // sombra suave onde a espuma encosta no corpo
                juce::ColourGradient sg (juce::Colours::transparentBlack, 44.0f, 0.0f, juce::Colours::black.withAlpha (0.45f), 60.0f, 0.0f, false);
                g.setGradientFill (sg);
                g.fillRect (44.0f, -50.0f, 18.0f, 100.0f);
            }
        }

        // ---------------------------------------------------------------- Sennheiser MD 421-II
        void md421() const
        {
            const juce::Colour body (0xff1b1c1f), basket (0xff26272b), ring (0xff6f7279), clip (0xff141517);
            // presilha (MZA 421) embaixo, perto da traseira
            {
                juce::Path c; c.addRoundedRectangle (150.0f, 14.0f, 46.0f, 16.0f, 4.0f);
                if (shadow) fillShadowOnly (c);
                else { shade (c, 8.0f, clip, 0.25f); edge (c, 0.06f); }
            }
            // corpo: afina da cesta (≈48 mm) para a traseira (≈32 mm)
            auto bodyP = profile ({ { 80.0f, 23.5f }, { 150.0f, 21.5f }, { 196.0f, 19.0f }, { 210.0f, 16.0f }, { 215.0f, 14.0f } }, 3.0f);
            shade (bodyP, 24.0f, body, 0.38f);
            edge (bodyP, 0.08f);
            // anel de graves (M ... S) com marcações
            auto rr = profile ({ { 168.0f, 21.0f }, { 182.0f, 20.3f } }, 1.0f);
            shade (rr, 21.0f, ring, 0.5f);
            if (! shadow)
            {
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                for (float x = 169.0f; x < 182.0f; x += 1.6f) g.fillRect (x, -20.5f, 0.6f, 41.0f);
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                for (int i = 0; i < 5; ++i) g.fillEllipse (154.0f + i * 2.6f - 0.6f, -1.0f - 16.0f, 1.2f, 1.2f);
            }
            // cesta frontal: um pouco mais larga, com fendas transversais
            auto head = profile ({ { 0.0f, 20.0f }, { 3.0f, 23.0f }, { 74.0f, 25.0f }, { 80.0f, 24.5f } }, 4.0f);
            shade (head, 25.0f, basket, 0.42f);
            if (! shadow)
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (head);
                for (float x = 6.0f; x < 70.0f; x += 2.9f)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.75f));
                    g.fillRoundedRectangle (x, -19.0f, 1.5f, 38.0f, 0.7f);
                    g.setColour (juce::Colours::white.withAlpha (0.07f));
                    g.fillRect (x + 1.6f, -19.0f, 0.4f, 38.0f);
                }
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.fillRect (74.0f, -25.0f, 1.2f, 50.0f);
            }
            edge (head, 0.10f);
        }
    };
}

// Foto da 4x12 (recorte do canto de cima à esquerda, 710 x 720 px): falante de cima à esquerda
// com centro em (282, 268) e raio de 182 px; o logo fica entre os 4 falantes.
namespace { constexpr float photoW = 710.0f, photoH = 720.0f, spkX = 282.0f, spkY = 268.0f, spkR = 182.0f; }

juce::Rectangle<float> SpeakerView::photoRect() const
{
    // cobre a área toda, presa no canto de cima à esquerda (o resto da caixa fica fora da tela)
    const float sc = juce::jmax ((float) getWidth() / photoW, (float) getHeight() / photoH);
    return { 0.0f, 0.0f, photoW * sc, photoH * sc };
}

juce::Point<float> SpeakerView::centre() const
{
    const auto r = photoRect();
    return { r.getX() + spkX / photoW * r.getWidth(), r.getY() + spkY / photoH * r.getHeight() };
}
float SpeakerView::speakerRadius() const { return spkR / photoW * photoRect().getWidth(); }
float SpeakerView::coneRadius() const { return speakerRadius() * 0.80f; }

void SpeakerView::renderCab()
{
    const float scale = 2.0f;
    background = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) (getWidth() * scale)), juce::jmax (1, (int) (getHeight() * scale)), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (cabPhoto, photoRect(), juce::RectanglePlacement::stretchToFit);
}

void SpeakerView::paint (juce::Graphics& g)
{
    if (background.isNull() || background.getWidth() != getWidth() * 2)
        renderCab();
    g.drawImage (background, getLocalBounds().toFloat());

    if (! active)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 22.0f);
        g.setColour (textDim);
        g.setFont (Fonts::condensed (18.0f));
        g.drawFittedText (lang::t ("Mic position is part of your IR", "A posição do microfone já faz parte do seu IR"), getLocalBounds().reduced (60), juce::Justification::centred, 2);
        return;
    }

    const float mx = px->getValue(), md = pd->getValue();
    const int model = (int) state.getRawParameterValue (ParamID::mic)->load();
    const float ang = state.getRawParameterValue (ParamID::micAngle)->load();
    const auto c = centre();
    const float rc = coneRadius();
    const juce::Point<float> m (c.x + std::cos (angle) * mx * rc, c.y + std::sin (angle) * mx * rc);

    // escala real: o falante tem 12" (305 mm); mais longe da tela = mais perto de quem olha
    const float k = speakerRadius() * 2.0f / 305.0f * (1.0f + 0.22f * md);
    const float shadowOff = 4.0f + 26.0f * md;

    // Fotos dos microfones (vistos por trás, cápsula apontada para o falante).
    // Âncora = ponto da cápsula na imagem; ppm = pixels por milímetro da imagem.
    struct Sprite { float ax, ay, ppm; };
    static const Sprite spr[3][2] = {
        { { 67.5f, 9.0f, 4.057f },  { 169.1f, 31.2f, 4.650f } },   // SM57
        { { 148.3f, 10.5f, 2.983f }, { 268.3f, 22.3f, 2.939f } },  // SM7B
        { { 107.3f, 8.6f, 3.489f },  { 132.7f, 20.1f, 3.512f } } };// MD421
    const int mi = model == vlv::mics::SM7B ? 1 : (model == vlv::mics::MD421 ? 2 : 0);
    const int off = ang >= 15.0f ? 1 : 0;                            // inclinado: troca para a foto fora de eixo
    const auto& sp = spr[mi][off];
    const auto& img = micImg[mi][off];
    const float sc = k / sp.ppm;
    const auto xf = juce::AffineTransform::translation (-sp.ax, -sp.ay).scaled (sc).translated (m);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    // sombra na tela do gabinete (luz de cima à esquerda), suave
    for (int i = 0; i < 4; ++i)
    {
        const float o = shadowOff * (0.7f + 0.15f * (float) i);
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.drawImageTransformed (img, xf.translated (o, o), true);
    }
    g.setOpacity (1.0f);
    g.drawImageTransformed (img, xf);

    // marca do ponto de captação
    const float fr = (model == vlv::mics::SM7B ? 46.0f : (model == vlv::mics::MD421 ? 23.0f : 16.0f)) * k + 5.0f;
    g.setColour (accent.withAlpha (0.9f));
    g.drawEllipse (m.x - fr, m.y - fr, fr * 2.0f, fr * 2.0f, 1.6f);
}

void SpeakerView::setFromMouse (juce::Point<float> p)
{
    const auto c = centre();
    const auto d = p - c;
    const float r = d.getDistanceFromOrigin();
    if (r > 1.0f) angle = std::atan2 (d.y, d.x);
    px->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, r / coneRadius()));
    repaint();
}

void SpeakerView::mouseDown (const juce::MouseEvent& e) { if (! active) return; px->beginChangeGesture(); setFromMouse (e.position); }
void SpeakerView::mouseDrag (const juce::MouseEvent& e) { if (active) setFromMouse (e.position); }
void SpeakerView::mouseUp (const juce::MouseEvent&)     { if (active) px->endChangeGesture(); }

//==============================================================================
CabPage::CabPage (ValvedProcessor& p) : proc (p), speaker (p.apvts)
{
    const auto names = vlv::getCabNames();
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = cabButtons.add (new juce::TextButton (names[i]));
        b->onClick = [this, i]
        {
            if (i == vlv::CabUserIR && proc.getUserIRName().isEmpty()) { chooseIR(); return; }
            setCab (i);
        };
        addAndMakeVisible (b);
    }

    loadIR.setButtonText ("LOAD IR...");
    loadIR.onClick = [this] { chooseIR(); };
    addAndMakeVisible (loadIR);

    irName.setFont (Fonts::condensed (15.0f));
    irName.setColour (juce::Label::textColourId, textDim);
    irName.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (irName);

    // Chave do IR: só fica para cima (LED aceso) quando há um IR carregado e em uso.
    // Sem IR carregado, clicar abre o seletor de arquivo.
    irSwitch.setTooltip ("IR: up = your loaded impulse response, down = built-in cabinets");
    irSwitch.onClick = [this]
    {
        if (proc.getUserIRName().isEmpty()) { chooseIR(); return; }
        const int cab = (int) proc.apvts.getRawParameterValue (ParamID::cab)->load();
        setCab (cab == vlv::CabUserIR ? lastBuiltInCab : (int) vlv::CabUserIR);
    };
    addAndMakeVisible (irSwitch);

    cabOn.setClickingTogglesState (true);
    cabOn.setTooltip ("Cabinet simulation on/off (off = send the amp without cab, e.g. to an external IR loader)");
    addAndMakeVisible (cabOn);
    cabOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ParamID::cabOn, cabOn);

    distance.setSliderStyle (juce::Slider::LinearHorizontal);
    distance.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    distance.setColour (juce::Slider::trackColourId, accent);
    distance.setColour (juce::Slider::backgroundColourId, line);
    distance.setColour (juce::Slider::thumbColourId, text);
    addAndMakeVisible (distance);
    distanceAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ParamID::micDist, distance);

    // Seleção do microfone
    const auto micNames = vlv::getMicNames();
    for (int i = 0; i < micNames.size(); ++i)
    {
        auto* b = micButtons.add (new juce::TextButton (micNames[i]));
        b->onClick = [this, i]
        {
            if (auto* prm = proc.apvts.getParameter (ParamID::mic))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                prm->endChangeGesture();
            }
        };
        addAndMakeVisible (b);
    }
    for (auto* b : { &bassCut, &presence })
    {
        b->setClickingTogglesState (true);
        addChildComponent (b);
    }
    for (int i = 0; i < 5; ++i)
    {
        auto* b = mdBass.add (new juce::TextButton (juce::StringArray { "M", "2", "3", "4", "S" }[i]));
        b->setTooltip ("MD421 bass switch: M = music (flat) .. S = speech (max cut)");
        b->onClick = [this, i]
        {
            if (auto* prm = proc.apvts.getParameter (ParamID::md421Bass))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                prm->endChangeGesture();
            }
        };
        addChildComponent (b);
    }
    bassCutAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ParamID::micBassCut, bassCut);
    presenceAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ParamID::micPresence, presence);

    angle.setSliderStyle (juce::Slider::LinearHorizontal);
    angle.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    angle.setColour (juce::Slider::trackColourId, accent);
    angle.setColour (juce::Slider::backgroundColourId, line);
    angle.setColour (juce::Slider::thumbColourId, text);
    addAndMakeVisible (angle);
    angleAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ParamID::micAngle, angle);

    addAndMakeVisible (speaker);
}

CabPage::~CabPage() = default;

void CabPage::setCab (int i)
{
    if (auto* prm = proc.apvts.getParameter (ParamID::cab))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
        prm->endChangeGesture();
    }
    if (i == vlv::CabUserIR)
        if (auto* on = proc.apvts.getParameter (ParamID::cabOn))
            if (on->getValue() < 0.5f) { on->beginChangeGesture(); on->setValueNotifyingHost (1.0f); on->endChangeGesture(); }
    refresh();
}

void CabPage::chooseIR()
{
    chooser = std::make_unique<juce::FileChooser> (lang::t ("Choose an impulse response (.wav)", "Escolha uma resposta ao impulso (.wav)"),
                                                   juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                   "*.wav;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.loadUserIR (f);
                          });
}

void CabPage::refresh()
{
    const int cab = (int) proc.apvts.getRawParameterValue (ParamID::cab)->load();
    for (int i = 0; i < cabButtons.size(); ++i)
        cabButtons[i]->setToggleState (i == cab, juce::dontSendNotification);
    const auto name = proc.getUserIRName();
    irName.setText (name.isEmpty() ? lang::t ("No IR loaded", "Nenhum IR carregado") : name, juce::dontSendNotification);
    const bool user = cab == vlv::CabUserIR;
    if (! user) lastBuiltInCab = cab;
    irSwitch.setToggleState (user && name.isNotEmpty(), juce::dontSendNotification);
    irSwitch.repaint();
    speaker.setActive (! user);
    distance.setEnabled (! user);
    angle.setEnabled (! user);
    const int mic = (int) proc.apvts.getRawParameterValue (ParamID::mic)->load();
    for (int i = 0; i < micButtons.size(); ++i)
    {
        micButtons[i]->setToggleState (i == mic, juce::dontSendNotification);
        micButtons[i]->setEnabled (! user);
    }
    const bool sm7b = mic == vlv::mics::SM7B && ! user;
    bassCut.setVisible (sm7b);
    presence.setVisible (sm7b);
    const bool md = mic == vlv::mics::MD421 && ! user;
    const int mdPos = (int) proc.apvts.getRawParameterValue (ParamID::md421Bass)->load();
    for (int i = 0; i < mdBass.size(); ++i)
    {
        mdBass[i]->setVisible (md);
        mdBass[i]->setToggleState (i == mdPos, juce::dontSendNotification);
    }
    speaker.repaint();
    repaint (micPanel);
    repaint (switchesRow);
}

void CabPage::paint (juce::Graphics& g)
{
    for (auto pr : { left, micPanel })
    {
        g.setColour (panel);
        g.fillRoundedRectangle (pr.toFloat(), 10.0f);
        g.setColour (line);
        g.drawRoundedRectangle (pr.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }

    g.setColour (textDim);
    g.setFont (Fonts::condensedBold (15.0f));
    g.drawText (lang::t ("CABINET", "GABINETE"), left.reduced (22).removeFromTop (24), juce::Justification::centredLeft);
    g.setFont (Fonts::condensedBold (12.0f));
    g.drawFittedText (lang::t ("CAB SIM", "SIMULAÇÃO"), juce::Rectangle<int> (cabOn.getX() - 64, cabOn.getY(), 58, cabOn.getHeight()), juce::Justification::centredRight, 1, 0.7f);
    g.setFont (Fonts::condensedBold (15.0f));
    g.drawText (lang::t ("IMPULSE RESPONSE", "RESPOSTA AO IMPULSO (IR)"), juce::Rectangle<int> (left.getX() + 22, loadIR.getY() - 28, 200, 20), juce::Justification::centredLeft);

    // Chave do IR: estado
    {
        const bool loaded = proc.getUserIRName().isNotEmpty();
        const bool onIR = irSwitch.getToggleState();
        auto t = switchesRow.withTrimmedLeft (irSwitch.getRight() - switchesRow.getX() + 14);
        g.setFont (Fonts::condensedBold (18.0f));
        g.setColour (onIR ? text : textDim);
        g.drawText ("IR", t.removeFromTop (t.getHeight() / 2).withTrimmedTop (6), juce::Justification::bottomLeft);
        g.setFont (Fonts::condensed (13.0f));
        g.setColour (onIR ? juce::Colour (0xff3cff7a) : textDim);
        g.drawText (onIR ? lang::t ("ON  -  using your IR", "LIGADO  -  usando o seu IR") : (loaded ? lang::t ("OFF  -  built-in cabinet", "DESLIGADO  -  caixa do plugin") : lang::t ("No IR loaded", "Nenhum IR carregado")), t.withTrimmedBottom (6), juce::Justification::topLeft);
    }

    // Painel do microfone
    auto mp = micPanel.reduced (18);
    g.setColour (textDim);
    g.setFont (Fonts::condensedBold (15.0f));
    g.drawText (lang::t ("MICROPHONE", "MICROFONE"), mp.removeFromTop (24), juce::Justification::centredLeft);

    const float md = proc.apvts.getRawParameterValue (ParamID::micDist)->load();
    const float ma = proc.apvts.getRawParameterValue (ParamID::micAngle)->load();
    auto dl = distance.getBounds();
    g.setFont (Fonts::condensedBold (14.0f));
    g.drawText (lang::t ("DISTANCE  ", "DISTÂNCIA  ") + juce::String (juce::roundToInt (vlv::mics::distanceMm (md))) + " mm",
                dl.withY (dl.getY() - 20).withHeight (18), juce::Justification::centredLeft);
    g.setFont (Fonts::condensed (13.0f));
    g.drawText (lang::t ("ON GRILLE", "NA TELA"), dl.withY (dl.getBottom()).withHeight (16), juce::Justification::centredLeft);
    g.drawText ("30 cm", dl.withY (dl.getBottom()).withHeight (16), juce::Justification::centredRight);
    auto al = angle.getBounds();
    g.setFont (Fonts::condensedBold (14.0f));
    g.drawText (lang::t ("ANGLE  ", "ÂNGULO  ") + juce::String (juce::roundToInt (ma)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")),
                al.withY (al.getY() - 20).withHeight (18), juce::Justification::centredLeft);
    g.setFont (Fonts::condensed (13.0f));
    g.drawText (lang::t ("ON AXIS", "NO EIXO"), al.withY (al.getBottom()).withHeight (16), juce::Justification::centredLeft);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("90\xc2\xb0")), al.withY (al.getBottom()).withHeight (16), juce::Justification::centredRight);

    g.setColour (textDim.withAlpha (0.8f));
    g.setFont (Fonts::condensed (13.5f));
    g.drawFittedText (lang::t ("Drag the mic on the speaker:\ncenter = brighter, edge = darker", "Arraste o microfone no falante:\ncentro = mais brilho, borda = mais escuro"),
                      micPanel.reduced (18).removeFromBottom (40), juce::Justification::centredLeft, 2);
}

void CabPage::resized()
{
    auto r = getLocalBounds().reduced (24, 12);
    left = r.removeFromLeft (290);
    r.removeFromLeft (20);
    micPanel = r.removeFromRight (220);
    r.removeFromRight (20);
    speaker.setBounds (r);

    // Painel esquerdo
    auto l = left.reduced (22);
    auto head = l.removeFromTop (24);
    cabOn.setBounds (head.removeFromRight (46).withSizeKeepingCentre (46, 22));
    l.removeFromTop (12);
    for (auto* b : cabButtons) { b->setBounds (l.removeFromTop (40)); l.removeFromTop (8); }
    l.removeFromTop (34);
    loadIR.setBounds (l.removeFromTop (40));
    l.removeFromTop (4);
    irName.setBounds (l.removeFromTop (24));
    l.removeFromTop (12);
    switchesRow = l.removeFromTop (70);
    irSwitch.setBounds (switchesRow.withWidth (30).withSizeKeepingCentre (30, 62));

    // Painel do microfone
    auto m = micPanel.reduced (18);
    m.removeFromTop (34);
    for (auto* b : micButtons) { b->setBounds (m.removeFromTop (36)); m.removeFromTop (8); }
    auto extra = m.removeFromTop (30);
    bassCut.setBounds (extra.withWidth (extra.getWidth() / 2 - 4));
    presence.setBounds (extra.withTrimmedLeft (extra.getWidth() / 2 + 4));
    {
        auto e = extra;
        const int bw5 = (e.getWidth() - 4 * 5) / 5;
        for (auto* b : mdBass) { b->setBounds (e.removeFromLeft (bw5)); e.removeFromLeft (5); }
    }
    m.removeFromTop (44);
    distance.setBounds (m.removeFromTop (22));
    m.removeFromTop (48);
    angle.setBounds (m.removeFromTop (22));
}
