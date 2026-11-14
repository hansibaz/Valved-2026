#include "CabPage.h"
#include "../PluginProcessor.h"
#include "../dsp/CabMics.h"

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
    // Eixo x = eixo do microfone (0 = frente/cápsula, positivo = para trás, até o conector).
    // Sombreamento cilíndrico: borda escura, reflexo no terço superior.
    struct MicPainter
    {
        juce::Graphics& g; bool shadow; float faceF;

        juce::ColourGradient cyl (float r, juce::Colour base, float shine = 0.35f) const
        {
            juce::ColourGradient cg (base.darker (0.9f), 0.0f, -r, base.darker (0.9f), 0.0f, r, false);
            cg.addColour (0.12, base.darker (0.2f));
            cg.addColour (0.30, base.brighter (shine));
            cg.addColour (0.42, base.brighter (shine * 0.4f));
            cg.addColour (0.70, base.darker (0.35f));
            return cg;
        }
        void fill (const juce::Path& p, float r, juce::Colour base, float shine = 0.35f) const
        {
            if (shadow) { g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillPath (p); return; }
            g.setGradientFill (cyl (r, base, shine));
            g.fillPath (p);
        }
        // tronco de cone de x0 (raio r0) até x1 (raio r1)
        static juce::Path section (float x0, float r0, float x1, float r1, float round = 0.0f)
        {
            juce::Path p;
            if (round <= 0.0f)
            {
                p.startNewSubPath (x0, -r0); p.lineTo (x1, -r1); p.lineTo (x1, r1); p.lineTo (x0, r0); p.closeSubPath();
            }
            else
                p.addRoundedRectangle (x0, -juce::jmax (r0, r1), x1 - x0, 2.0f * juce::jmax (r0, r1), round);
            return p;
        }
        // linhas finas ao longo da seção (textura de tela / frisos)
        void lines (float x0, float x1, float r, float step, juce::Colour c, bool across) const
        {
            if (shadow) return;
            g.setColour (c);
            if (across) for (float x = x0 + step * 0.5f; x < x1; x += step) g.fillRect (x - 0.35f, -r, 0.7f, 2.0f * r);
            else        for (float y = -r + step * 0.5f; y < r; y += step) g.fillRect (x0, y - 0.3f, x1 - x0, 0.6f);
        }
        // tela metálica (malha) recortada na seção
        void mesh (const juce::Path& clip, float x0, float x1, float r, float cell) const
        {
            if (shadow) return;
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (clip);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            for (float x = x0; x < x1; x += cell) g.fillRect (x, -r, cell * 0.38f, 2.0f * r);
            for (float y = -r; y < r; y += cell) g.fillRect (x0, y, x1 - x0, cell * 0.38f);
        }
        // face frontal (elipse: quanto mais fora do eixo, mais estreita)
        void face (float r, juce::Colour base, bool meshFace, float cell) const
        {
            const float w = r * faceF;
            juce::Path e; e.addEllipse (-w, -r, 2.0f * w, 2.0f * r);
            if (shadow) { g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillPath (e); return; }
            juce::ColourGradient fg (base.brighter (0.25f), -w, -r * 0.6f, base.darker (0.8f), w, r, false);
            g.setGradientFill (fg);
            g.fillPath (e);
            if (meshFace) mesh (e, -w, w, r, cell);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawEllipse (-w, -r, 2.0f * w, 2.0f * r, 0.8f);
        }

        // Shure SM57: 157 mm, tela de 32 mm, corpo cinza-chumbo afinando para trás
        void sm57() const
        {
            const juce::Colour grille (0xff3d3f43), body (0xff505157), ring (0xff1a1a1c);
            auto gr = section (0.0f, 16.0f, 38.0f, 16.0f, 3.0f);
            fill (gr, 16.0f, grille, 0.45f);
            mesh (gr, 0.0f, 38.0f, 16.0f, 1.6f);
            fill (section (37.0f, 15.6f, 41.0f, 15.0f), 15.6f, ring, 0.2f);                       // anel/ranhura
            juce::Path b; b.startNewSubPath (41.0f, -15.0f); b.quadraticTo (52.0f, -12.4f, 62.0f, -12.4f);
            b.lineTo (146.0f, -12.0f); b.lineTo (146.0f, 12.0f); b.lineTo (62.0f, 12.4f); b.quadraticTo (52.0f, 12.4f, 41.0f, 15.0f); b.closeSubPath();
            fill (b, 13.0f, body, 0.30f);
            fill (section (146.0f, 12.0f, 157.0f, 11.0f, 2.0f), 12.0f, juce::Colour (0xff6b6d72), 0.5f);  // conector
            face (16.0f, grille, true, 1.6f);
        }

        // Shure SM7B: 190 mm, espuma grande na frente, corpo preto, garfo (yoke) com manopla lateral
        void sm7b() const
        {
            const juce::Colour foam (0xff1b1b1d), body (0xff232327), yoke (0xff2e2f33);
            // corpo
            fill (section (40.0f, 31.0f, 178.0f, 31.0f, 6.0f), 31.0f, body, 0.45f);
            fill (section (176.0f, 29.0f, 190.0f, 27.0f, 3.0f), 29.0f, juce::Colour (0xff3a3b40), 0.4f);   // painel traseiro
            // garfo (yoke): braço lateral que sai do corpo, com a manopla grande
            {
                juce::Path y; y.addRoundedRectangle (92.0f, -36.0f, 46.0f, 72.0f, 10.0f);
                if (shadow) { g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillPath (y); }
                else
                {
                    juce::ColourGradient yg (yoke.brighter (0.3f), 92.0f, -36.0f, yoke.darker (0.7f), 138.0f, 36.0f, false);
                    g.setGradientFill (yg); g.fillPath (y);
                    g.setColour (juce::Colours::white.withAlpha (0.08f)); g.strokePath (y, juce::PathStrokeType (0.8f));
                }
                const float kr = 15.0f; const juce::Point<float> kc (115.0f, 0.0f);
                juce::Path k; k.addEllipse (kc.x - kr, kc.y - kr, kr * 2, kr * 2);
                if (shadow) { g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillPath (k); }
                else
                {
                    juce::ColourGradient kg (juce::Colour (0xffa9abb0), kc.x - kr, kc.y - kr, juce::Colour (0xff2c2d31), kc.x + kr, kc.y + kr, false);
                    g.setGradientFill (kg); g.fillPath (k);
                    g.setColour (juce::Colours::black.withAlpha (0.5f));
                    for (int i = 0; i < 16; ++i)
                    {
                        const float a = juce::MathConstants<float>::twoPi * i / 16.0f;
                        g.drawLine (kc.x + std::cos (a) * kr * 0.75f, kc.y + std::sin (a) * kr * 0.75f, kc.x + std::cos (a) * kr, kc.y + std::sin (a) * kr, 0.8f);
                    }
                    g.setColour (juce::Colour (0xff1c1c1f));
                    g.fillEllipse (kc.x - kr * 0.35f, kc.y - kr * 0.35f, kr * 0.7f, kr * 0.7f);
                }
            }
            // espuma (windscreen) bojuda
            juce::Path f; f.addRoundedRectangle (0.0f, -46.0f, 62.0f, 92.0f, 30.0f);
            fill (f, 46.0f, foam, 0.25f);
            if (! shadow)
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (f);
                juce::Random rng (5);
                for (int i = 0; i < 900; ++i)
                {
                    g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.08f));
                    g.fillRect (rng.nextFloat() * 62.0f, rng.nextFloat() * 92.0f - 46.0f, 0.9f, 0.9f);
                }
            }
            // frente da espuma: domo arredondado com textura
            {
                const float r = 46.0f, w = r * juce::jmax (0.55f, faceF);
                juce::Path e; e.addEllipse (-w, -r, 2.0f * w, 2.0f * r);
                if (shadow) { g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillPath (e); }
                else
                {
                    juce::ColourGradient fg (juce::Colour (0xff3a3a3e), -w * 0.4f, -r * 0.5f, juce::Colour (0xff0d0d0f), w * 0.6f, r * 0.8f, true);
                    g.setGradientFill (fg);
                    g.fillPath (e);
                    juce::Graphics::ScopedSaveState ss (g);
                    g.reduceClipRegion (e);
                    juce::Random rng (9);
                    for (int i = 0; i < 1400; ++i)
                    {
                        g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.10f));
                        g.fillRect (rng.nextFloat() * 2.0f * w - w, rng.nextFloat() * 2.0f * r - r, 0.9f, 0.9f);
                    }
                }
            }
        }

        // Sennheiser MD421-II: 200 mm, cesta frontal com frisos, corpo preto, anel de graves (5 posições)
        void md421() const
        {
            const juce::Colour basket (0xff2b2c30), body (0xff1f2023), ring (0xff707278);
            juce::Path fr; fr.startNewSubPath (0.0f, -21.0f); fr.lineTo (72.0f, -25.0f); fr.lineTo (72.0f, 25.0f); fr.lineTo (0.0f, 21.0f); fr.closeSubPath();
            fill (fr, 24.0f, basket, 0.40f);
            lines (2.0f, 70.0f, 22.0f, 2.6f, juce::Colours::black.withAlpha (0.55f), true);           // frisos (fendas)
            fill (section (72.0f, 25.0f, 76.0f, 25.0f), 25.0f, juce::Colour (0xff121214), 0.2f);
            fill (section (76.0f, 24.0f, 168.0f, 22.0f), 24.0f, body, 0.40f);
            // anel de graves com marcações
            fill (section (138.0f, 23.0f, 152.0f, 23.0f, 1.5f), 23.0f, ring, 0.45f);
            lines (139.0f, 151.0f, 23.0f, 2.4f, juce::Colours::black.withAlpha (0.35f), true);
            // traseira e conector
            fill (section (168.0f, 22.0f, 192.0f, 17.0f), 22.0f, body, 0.35f);
            fill (section (192.0f, 15.0f, 200.0f, 14.0f, 1.5f), 15.0f, juce::Colour (0xff5d5f64), 0.5f);
            // presilha (clip) integrada embaixo
            juce::Path c; c.addRoundedRectangle (150.0f, 18.0f, 34.0f, 12.0f, 3.0f);
            fill (c, 6.0f, juce::Colour (0xff17181a), 0.2f);
            face (21.0f, basket, true, 2.2f);
        }
    };
}

juce::Point<float> SpeakerView::centre() const
{
    const float R = speakerRadius();
    return { frameT + R * 1.16f, frameT + R * 1.16f };
}
float SpeakerView::speakerRadius() const
{
    // falante inteiro + metade do de baixo / um pedaço do da direita
    return juce::jmin (((float) getHeight() - frameT) / 2.95f, ((float) getWidth() - frameT) / 2.75f);
}
float SpeakerView::coneRadius() const { return speakerRadius() * 0.80f; }

void SpeakerView::renderCab()
{
    const float scale = 2.0f;
    const float W = (float) getWidth(), H = (float) getHeight();
    background = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) (W * scale)), juce::jmax (1, (int) (H * scale)), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale));

    // Caixa: tolex preto texturizado, canto arredondado (o resto da caixa continua fora da tela)
    juce::Path cabShape;
    cabShape.addRoundedRectangle (0.0f, 0.0f, W + 60.0f, H + 60.0f, 22.0f);
    g.reduceClipRegion (cabShape);
    g.setColour (juce::Colour (0xff141416));
    g.fillPath (cabShape);
    {
        juce::Random rng (3);
        for (int i = 0; i < (int) (W * H * 0.05f); ++i)
        {
            g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.06f + 0.05f * rng.nextFloat()));
            g.fillRect (rng.nextFloat() * W, rng.nextFloat() * H, 1.3f, 1.3f);
        }
        juce::ColourGradient edge (juce::Colours::white.withAlpha (0.10f), 0.0f, 0.0f, juce::Colours::transparentWhite, 26.0f, 26.0f, false);
        g.setGradientFill (edge);
        g.fillRect (0.0f, 0.0f, W, 26.0f);
        g.fillRect (0.0f, 0.0f, 26.0f, H);
    }

    // Baffle e falantes
    juce::Path grilleShape;
    grilleShape.addRoundedRectangle (frameT, frameT, W + 60.0f, H + 60.0f, 10.0f);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (grilleShape);
        g.setColour (juce::Colour (0xff0c0c0d));
        g.fillPath (grilleShape);
        const auto c = centre();
        const float R = speakerRadius();
        drawSpeaker (g, c, R, 11);
        drawSpeaker (g, c.translated (R * spacing, 0.0f), R, 23);
        drawSpeaker (g, c.translated (0.0f, R * spacing), R, 37);

        // Tela (grill cloth) preta com trama prateada, semitransparente: os falantes aparecem por trás
        g.setColour (juce::Colours::black.withAlpha (0.38f));
        g.fillPath (grilleShape);
        const float cell = 3.0f;
        for (float y = frameT; y < H; y += cell)
            for (float x = frameT; x < W; x += cell)
            {
                const bool h = ((int) ((x - frameT) / cell) + (int) ((y - frameT) / cell)) % 2 == 0;
                g.setColour (juce::Colour (0xffb8b4a8).withAlpha (h ? 0.10f : 0.05f));
                if (h) g.fillRect (x, y + cell * 0.35f, cell * 0.9f, cell * 0.3f);
                else   g.fillRect (x + cell * 0.35f, y, cell * 0.3f, cell * 0.9f);
            }
        // sombra da borda sobre a tela
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.6f), frameT, frameT, juce::Colours::transparentBlack, frameT + 16.0f, frameT + 16.0f, false);
        g.setGradientFill (sh);
        g.fillRect (frameT, frameT, W, 16.0f);
        g.fillRect (frameT, frameT, 16.0f, H);

        // Logo
        const juce::Colour cream (0xffeee4c8);
        const float lx = c.x + R * 1.05f, ly = c.y + R * 1.06f;
        auto logoR = juce::Rectangle<float> (lx, ly, W - lx - 8.0f, H - ly - 6.0f);
        {
            auto f = ui::Fonts::logo (100.0f);
            const float wAt100 = f.getStringWidthFloat ("Valved") * 1.08f;
            g.setFont (ui::Fonts::logo (juce::jmin (logoR.getHeight() * 0.9f, 60.0f, 100.0f * logoR.getWidth() / wAt100)));
        }
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawText ("Valved", logoR.translated (1.5f, 2.0f), juce::Justification::centred);
        g.setColour (cream);
        g.drawText ("Valved", logoR, juce::Justification::centred);
    }
    // Friso (piping) branco em volta da tela
    g.setColour (juce::Colour (0xffd9d4c4));
    juce::Path piping;
    piping.addRoundedRectangle (frameT - 3.0f, frameT - 3.0f, W + 60.0f, H + 60.0f, 12.0f);
    g.strokePath (piping, juce::PathStrokeType (2.2f));
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
        g.drawFittedText ("Mic position is part of your IR", getLocalBounds().reduced (60), juce::Justification::centred, 2);
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
    const float shadowOff = 5.0f + 30.0f * md;
    const float faceF = 0.62f - 0.42f * juce::jlimit (0.0f, 1.0f, ang / 90.0f);
    const auto xf = juce::AffineTransform::scale (k).rotated (-0.82f).translated (m);   // corpo sai para cima/direita

    auto drawMic = [&] (bool shadow)
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (shadow ? xf.translated (shadowOff, shadowOff) : xf);
        MicPainter mp { g, shadow, faceF };
        if (model == vlv::mics::SM7B) mp.sm7b();
        else if (model == vlv::mics::MD421) mp.md421();
        else mp.sm57();
    };
    drawMic (true);
    drawMic (false);

    // marca do ponto de captação
    const float fr = (model == vlv::mics::SM7B ? 46.0f : (model == vlv::mics::MD421 ? 21.0f : 16.0f)) * k + 5.0f;
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
    chooser = std::make_unique<juce::FileChooser> ("Choose an impulse response (.wav)",
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
    irName.setText (name.isEmpty() ? juce::String ("No IR loaded") : name, juce::dontSendNotification);
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
    g.drawText ("CABINET", left.reduced (22).removeFromTop (24), juce::Justification::centredLeft);
    g.setFont (Fonts::condensedBold (12.0f));
    g.drawText ("CAB SIM", juce::Rectangle<int> (cabOn.getX() - 64, cabOn.getY(), 58, cabOn.getHeight()), juce::Justification::centredRight);
    g.setFont (Fonts::condensedBold (15.0f));
    g.drawText ("IMPULSE RESPONSE", juce::Rectangle<int> (left.getX() + 22, loadIR.getY() - 28, 200, 20), juce::Justification::centredLeft);

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
        g.drawText (onIR ? "ON  -  using your IR" : (loaded ? "OFF  -  built-in cabinet" : "No IR loaded"), t.withTrimmedBottom (6), juce::Justification::topLeft);
    }

    // Painel do microfone
    auto mp = micPanel.reduced (18);
    g.setColour (textDim);
    g.setFont (Fonts::condensedBold (15.0f));
    g.drawText ("MICROPHONE", mp.removeFromTop (24), juce::Justification::centredLeft);

    const float md = proc.apvts.getRawParameterValue (ParamID::micDist)->load();
    const float ma = proc.apvts.getRawParameterValue (ParamID::micAngle)->load();
    auto dl = distance.getBounds();
    g.setFont (Fonts::condensedBold (14.0f));
    g.drawText ("DISTANCE  " + juce::String (juce::roundToInt (vlv::mics::distanceMm (md))) + " mm",
                dl.withY (dl.getY() - 20).withHeight (18), juce::Justification::centredLeft);
    g.setFont (Fonts::condensed (13.0f));
    g.drawText ("ON GRILLE", dl.withY (dl.getBottom()).withHeight (16), juce::Justification::centredLeft);
    g.drawText ("30 cm", dl.withY (dl.getBottom()).withHeight (16), juce::Justification::centredRight);
    auto al = angle.getBounds();
    g.setFont (Fonts::condensedBold (14.0f));
    g.drawText ("ANGLE  " + juce::String (juce::roundToInt (ma)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")),
                al.withY (al.getY() - 20).withHeight (18), juce::Justification::centredLeft);
    g.setFont (Fonts::condensed (13.0f));
    g.drawText ("ON AXIS", al.withY (al.getBottom()).withHeight (16), juce::Justification::centredLeft);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("90\xc2\xb0")), al.withY (al.getBottom()).withHeight (16), juce::Justification::centredRight);

    g.setColour (textDim.withAlpha (0.8f));
    g.setFont (Fonts::condensed (13.5f));
    g.drawFittedText ("Drag the mic on the speaker:\ncenter = brighter, edge = darker",
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
