# EquipmentPreview
This is an inventory mod for Skyrim LE that I'm currently working on as 09/11/2019.

The main concept of this mod is that a mannequin will be shown instead of the actual player when the user opens the inventory.

The user will be able to put clothes on the mannequin without actually adding items into their inventory and see how it looks on their characters.

There are four features at this moment:

  *Equip
  *Unequip all
  *Sync from mannequin to player
  *Sync from player to mannequin
  
Also, if you have HDT PE installed, havok physics objects will keep enabled even in the inventory menu.


To build this project, you'll need SKSE 1.7.32 with a modification on GameEvents.h

You need to put "protected:" on line 18 like this:

```
...
class EventDispatcher
{
protected:
	typedef BSTEventSink<EventT> SinkT;

	SimpleLock			lock;				// 000
...
```
so the custom EventDispatcher class MenuOpenCloseEventSource can access to SimpleLock object.
