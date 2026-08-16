#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class PluginBrowserComponent final : public juce::Component,
                                     private juce::Timer,
                                     private juce::ListBoxModel
{
public:
    using LoadCallback = std::function<void (int, const juce::PluginDescription&)>;
    using CloseCallback = std::function<void()>;

    PluginBrowserComponent (LoadCallback loadCallbackToUse,
                            CloseCallback closeCallbackToUse);
    ~PluginBrowserComponent() override;

    void showForSlot (int slotIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class ScanThread;

    int getNumRows() override;
    void paintListBoxItem (int rowNumber,
                           juce::Graphics&,
                           int width,
                           int height,
                           bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void timerCallback() override;
    void refreshCatalog();
    void rebuildFilter();
    void loadSelected();

    void startScan();
    void runScan (ScanThread& thread);
    void setScanStatus (juce::String text);
    juce::String getScanStatus() const;

    void showPathsMenu();
    void choosePathsToAdd();
    void resetDefaultPaths();
    void removePath (int index);
    juce::FileSearchPath getSearchPath() const;
    void loadPaths();
    void savePaths() const;
    void loadCatalog();
    void saveCatalog();

    juce::File getSettingsDirectory() const;
    juce::File getCatalogFile() const;
    juce::File getPathsFile() const;
    juce::File getDeadMansPedalFile() const;

    LoadCallback loadCallback;
    CloseCallback closeCallback;
    int targetSlot = 0;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::Array<juce::PluginDescription> catalog;
    std::vector<int> filteredIndices;
    juce::StringArray searchPaths;

    juce::TextEditor searchBox;
    juce::TextButton pathsButton { "PATHS" };
    juce::TextButton scanButton { "SCAN" };
    juce::TextButton loadButton { "LOAD" };
    juce::TextButton closeButton { "CLOSE" };
    juce::ListBox resultList { "Plugin Browser", this };
    juce::Label statusLabel;

    std::unique_ptr<juce::FileChooser> pathChooser;
    std::unique_ptr<ScanThread> scanThread;
    std::atomic<bool> scanning { false };
    std::atomic<float> scanProgress { 0.0f };
    std::atomic<uint64_t> catalogVersion { 0 };
    uint64_t displayedCatalogVersion = 0;

    mutable juce::CriticalSection statusLock;
    juce::String scanStatus { "READY" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginBrowserComponent)
};
