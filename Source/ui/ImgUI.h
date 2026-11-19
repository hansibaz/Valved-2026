#pragma once
// Interfaces em imagem (pedais e efeitos): as fotos dos UI kits ficam desenhadas como estão,
// e os controles (knobs, chaves, LEDs) vão por cima, nas posições medidas na foto original.
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

namespace imgui
{
    inline juce::Image load (const void* d, int s) { return juce::ImageCache::getFromMemory (d, s); }

    // Converte coordenadas da foto original (px) para a tela.
    // nx, ny, nw, nh: retângulo útil da foto (o recorte sem as bordas transparentes)
    struct Mapper
    {
        juce::Rectangle<float> area;
        float nx = 0, ny = 0, nw = 1, nh = 1;
        float sc() const { return area.getWidth() / nw; }
        juce::Point<float> pt (float x, float y) const { return { area.getX() + (x - nx) * sc(), area.getY() + (y - ny) * sc() }; }
        juce::Rectangle<float> box (float cx, float cy, float w, float h) const { return juce::Rectangle<float> (w * sc(), h * sc()).withCentre (pt (cx, cy)); }
        juce::Rectangle<float> rect (float x, float y, float w, float h) const { return { pt (x, y).x, pt (x, y).y, w * sc(), h * sc() }; }
    };

    // Desenha uma imagem (com alfa) num retângulo
    inline void draw (juce::Graphics& g, const juce::Image& im, juce::Rectangle<float> r, float alpha = 1.0f)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (alpha);
        g.drawImage (im, r, juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);
    }

    // Botão invisível (a própria foto mostra a chave); ao pisar, escurece um pouco
    struct HitButton : public juce::Button
    {
        explicit HitButton (bool latching = true) : juce::Button ("hit") { setClickingTogglesState (latching); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        juce::Image overlay;   // opcional: imagem do botão (desenhada sempre)
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto r = getLocalBounds().toFloat();
            if (overlay.isValid()) draw (g, overlay, r);
            if (down || highlighted)
            {
                g.setColour (juce::Colours::black.withAlpha (down ? 0.28f : 0.0f));
                g.fillEllipse (r.reduced (r.getWidth() * 0.18f));
                if (highlighted && ! down) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillEllipse (r.reduced (r.getWidth() * 0.18f)); }
            }
        }
    };
}
