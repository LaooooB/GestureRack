from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / 'Source' / 'RackInteraction.cpp'
text = path.read_text(encoding='utf-8')
old = '''    graphManager.rebuildSerialConnections (\n        juce::jmin (\n            getMainBusNumInputChannels(),\n            getMainBusNumOutputChannels()));'''
new = '''    graphManager.rebuildRouting (buildHostBusLayout());'''
if text.count(old) != 1:
    raise RuntimeError(f'RackInteraction.cpp expected one legacy rebuild call, got {text.count(old)}')
path.write_text(text.replace(old, new, 1), encoding='utf-8', newline='\n')
print('RackInteraction graph rebuild call migrated.')
