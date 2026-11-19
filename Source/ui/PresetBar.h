#pragma once
// Barra de presets do cabeçalho:  [<]  [ nome do preset  v ]  [>]  [SAVE]
// Clicar no nome abre a lista (10 de fábrica + 50 do usuário). SAVE grava num dos 50 espaços.
#include <juce_gui_basics/juce_gui_basics.h>
#include "Style.h"
#include "../Presets.h"

class PresetBar : public juce::Component
{
public:
    explicit PresetBar (ValvedProcessor& p) : proc (p), pm (p)
    {
        for (auto* b : { &prev, &next, &save })
            addAndMakeVisible (b);
        prev.setTooltip ("Previous preset");
        next.setTooltip ("Next preset");
        save.setTooltip ("Save the current sound in one of the 50 user slots");
        prev.onClick = [this] { pm.step (-1); repaint(); };
        next.onClick = [this] { pm.step (+1); repaint(); };
        save.onClick = [this] { openSave(); };
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    // chamado pelo timer do editor
    void tick()
    {
        const bool mod = pm.isModified();
        const auto nm = pm.currentName();
        if (mod != shownModified || nm != shownName) { shownModified = mod; shownName = nm; repaint(); }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (22);   // legenda PRESET
        save.setBounds (r.removeFromRight (52));
        r.removeFromRight (6);
        prev.setBounds (r.removeFromLeft (28));
        next.setBounds (r.removeFromRight (28));
        nameBox = r.reduced (4, 0);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace ui;
        const auto b = nameBox.toFloat();
        g.setColour (juce::Colour (0xff0d0e10));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (hover ? 0.28f : 0.14f));
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
        const int idx = pm.currentIndex();
        juce::String tag = idx < 0 ? juce::String() : idx < PresetManager::NumFactory ? juce::String ("F") + juce::String (idx + 1)
                                                                                      : juce::String ("U") + juce::String (idx - PresetManager::NumFactory + 1).paddedLeft ('0', 2);
        auto t = b.reduced (10.0f, 0.0f);
        if (tag.isNotEmpty())
        {
            g.setFont (Fonts::condensedBold (13.0f));
            g.setColour (accent.withAlpha (0.85f));
            g.drawText (tag, t.removeFromLeft (28.0f), juce::Justification::centredLeft);
        }
        // seta da lista
        {
            auto a = t.removeFromRight (12.0f);
            juce::Path p; const auto c = a.getCentre();
            p.addTriangle (c.x - 4.5f, c.y - 2.0f, c.x + 4.5f, c.y - 2.0f, c.x, c.y + 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillPath (p);
        }
        g.setFont (Fonts::condensedMedium (16.0f));
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        juce::String nm = shownName.isEmpty() ? lang::t ("No preset", "Sem preset") : shownName;
        if (shownModified) nm << " *";
        g.drawFittedText (nm, t.toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
        // legenda
        g.setFont (Fonts::condensed (12.0f));
        g.setColour (textDim);
        g.drawText ("PRESET", juce::Rectangle<float> (b.getX(), b.getY() - 20.0f, 80.0f, 16.0f), juce::Justification::centredLeft);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (nameBox.contains (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override  { setHover (false); }
    void mouseDown (const juce::MouseEvent& e) override { if (nameBox.contains (e.getPosition())) openMenu(); }

private:
    void setHover (bool h) { if (h != hover) { hover = h; repaint(); } }

    void openMenu()
    {
        juce::PopupMenu m, user;
        const int cur = pm.currentIndex();
        m.addSectionHeader (lang::t ("FACTORY", "FÁBRICA"));
        const auto fn = PresetManager::factoryNames();
        for (int i = 0; i < PresetManager::NumFactory; ++i)
            m.addItem (1 + i, fn[i], true, cur == i);
        int filled = 0;
        for (int s = 0; s < PresetManager::NumUser; ++s)
        {
            const auto nm = pm.userName (s);
            const auto label = juce::String (s + 1).paddedLeft ('0', 2) + "   " + (nm.isEmpty() ? lang::t ("(empty)", "(vazio)") : nm);
            user.addItem (1 + PresetManager::NumFactory + s, label, nm.isNotEmpty(), cur == PresetManager::NumFactory + s);
            if (nm.isNotEmpty()) ++filled;
        }
        m.addSeparator();
        m.addSectionHeader (lang::t ("USER  (", "USUÁRIO  (") + juce::String (filled) + " / " + juce::String (PresetManager::NumUser) + ")");
        m.addSubMenu (lang::t ("User presets", "Presets do usuário"), user);
        m.addSeparator();
        m.addItem (1000, lang::t ("Save...", "Salvar..."));
        const bool isUser = cur >= PresetManager::NumFactory;
        m.addItem (1001, lang::t ("Delete \"", "Apagar \"") + pm.currentName() + "\"", isUser);
        m.addItem (1002, lang::t ("Open presets folder", "Abrir a pasta dos presets"));

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                         [safe = juce::Component::SafePointer<PresetBar> (this)] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& self = *safe;
            if (r == 1000) { self.openSave(); return; }
            if (r == 1001) { self.confirmDelete(); return; }
            if (r == 1002) { PresetManager::userDir().createDirectory(); PresetManager::userDir().startAsProcess(); return; }
            self.pm.load (r - 1);
            self.repaint();
        });
    }

    void openSave()
    {
        auto* w = new juce::AlertWindow (lang::t ("Save preset", "Salvar preset"), lang::t ("Choose a name and one of the 50 user slots.", "Escolha um nome e um dos 50 espaços do usuário."), juce::MessageBoxIconType::NoIcon, this);
        const int cur = pm.currentIndex();
        int defSlot = cur >= PresetManager::NumFactory ? cur - PresetManager::NumFactory : -1;
        juce::StringArray slots;
        for (int s = 0; s < PresetManager::NumUser; ++s)
        {
            const auto nm = pm.userName (s);
            if (defSlot < 0 && nm.isEmpty()) defSlot = s;
            slots.add (juce::String (s + 1).paddedLeft ('0', 2) + "   " + (nm.isEmpty() ? juce::String ("(empty)") : nm));
        }
        if (defSlot < 0) defSlot = 0;
        w->addTextEditor ("name", pm.currentName().isEmpty() ? lang::t ("My preset", "Meu preset") : pm.currentName(), lang::t ("Name", "Nome"));
        w->addComboBox ("slot", slots, lang::t ("Slot", "Espaço"));
        w->getComboBoxComponent ("slot")->setSelectedItemIndex (defSlot);
        w->addButton (lang::t ("SAVE", "SALVAR"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (lang::t ("CANCEL", "CANCELAR"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<PresetBar> (this), w] (int r)
        {
            if (safe == nullptr || r != 1) return;
            auto name = w->getTextEditorContents ("name").trim();
            if (name.isEmpty()) name = lang::t ("My preset", "Meu preset");
            const int slot = w->getComboBoxComponent ("slot")->getSelectedItemIndex();
            if (safe->pm.userExists (slot) && slot != safe->pm.currentIndex() - PresetManager::NumFactory)
            {
                const auto old = safe->pm.userName (slot);
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, lang::t ("Replace preset?", "Substituir preset?"),
                    lang::t ("Slot ", "O espaço ") + juce::String (slot + 1).paddedLeft ('0', 2) + lang::t (" already has \"", " já tem \"") + old + lang::t ("\". Replace it with \"", "\". Substituir por \"") + name + "\"?",
                    lang::t ("REPLACE", "SUBSTITUIR"), lang::t ("CANCEL", "CANCELAR"), safe.getComponent(),
                    juce::ModalCallbackFunction::create ([safe, slot, name] (int ok)
                    {
                        if (safe != nullptr && ok == 1) { safe->pm.save (slot, name); safe->repaint(); }
                    }));
                return;
            }
            safe->pm.save (slot, name);
            safe->repaint();
        }), true);
    }

    void confirmDelete()
    {
        const int slot = pm.currentIndex() - PresetManager::NumFactory;
        if (slot < 0) return;
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, lang::t ("Delete preset?", "Apagar preset?"),
            lang::t ("Delete \"", "Apagar \"") + pm.currentName() + lang::t ("\" from slot ", "\" do espaço ") + juce::String (slot + 1).paddedLeft ('0', 2) + "?",
            lang::t ("DELETE", "APAGAR"), lang::t ("CANCEL", "CANCELAR"), this,
            juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<PresetBar> (this), slot] (int ok)
            {
                if (safe != nullptr && ok == 1) { safe->pm.remove (slot); safe->repaint(); }
            }));
    }

    ValvedProcessor& proc;
    PresetManager pm;
    juce::TextButton prev { "<" }, next { ">" }, save { "SAVE" };
    juce::Rectangle<int> nameBox;
    juce::String shownName;
    bool shownModified = false, hover = false;
};
