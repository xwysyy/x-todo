# Multilevel List Rules

X-TODO represents hierarchy inside one list tab. List tabs and item hierarchy
are separate concepts.

## Data Shape

Each `TodoItem` has a `level` in the range `0..3` and a `collapsed` flag.
The item vector remains a flat preorder list. A subtree starts at an item and
continues while following items have a greater level than the root item.

The storage header for hierarchical items is `XTODO v4`. Item rows are:

```text
item <0|1> <level> <0|1 collapsed> <escaped text>
```

The reader must continue to accept `XTODO v2` and `XTODO v3`. `v2` item rows
do not carry a level, so they load as `level = 0` and `collapsed = false`.
`v3` item rows carry a level but no collapsed flag, so they load as
`collapsed = false`.

## Interaction

Hierarchy operations belong to the item surface, not the title-bar menu or the
tray menu. The edit control handles `Tab` as indent and `Shift+Tab` as outdent.
Indenting uses the previous visible active row as the parent candidate. Hidden
descendants under a collapsed item must not affect the parent chosen by `Tab`.

Checking an item applies to that item and its subtree. Deleting an item removes
that item and its subtree. Dragging an item moves the whole subtree as one
block inside the active section.

Moving a subtree between active and completed sections makes that subtree an
independent block in the target section. The moved block is rebased so its root
loads and renders at `level = 0`, while descendants keep their relative levels
inside that block. The model does not keep a cross-section parent pointer, so
restoring a completed child returns the completed block to the active boundary
as its own root.

Items with children expose an inline disclosure control. Collapsing an item
hides its descendants but does not remove them or exclude them from subtree
operations. The completed section keeps its existing expand/collapse behavior.

## Rendering

Indentation moves the checkbox, text, and edit box together. Hit-testing must
use the same rectangles that painting uses. The disclosure control, checkbox,
text, delete button, and drag handle are one row geometry contract. The row
hover surface stays full width so deep items remain easy to target in the
narrow note window.
