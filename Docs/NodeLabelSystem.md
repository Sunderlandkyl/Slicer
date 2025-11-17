# Generic Node Label Display System

## Overview

This implementation creates a generic, extensible system for displaying text labels attached to any MRML node in both 2D and 3D views. The system is designed to supersede the node-specific label positioning found in Markups and make it available to all node types, including Segmentations.

## Key Features

1. **Universal Node Support**: Works with any `vtkMRMLDisplayableNode`, including:
   - Markups (fiducials, lines, curves, etc.)
   - Segmentations
   - Models
   - Volumes
   - Any custom displayable node

2. **Flexible Anchor Positioning**:
   - **Manual**: User-specified position in RAS coordinates
   - **NodeCenter**: Automatic center of node's bounding box
   - **SegmentLargestIsland**: Center of mass of largest segment island on slice (for segmentations)
   - **Custom**: Extensible for custom anchor calculations

3. **Label Position Modes**:
   - **Default**: At anchor point
   - **Left**: Left edge of view
   - **Right**: Right edge of view
   - **Top**: Top edge of view
   - **Bottom**: Bottom edge of view

4. **Collision Avoidance**: Labels with the same position preference automatically adjust their positions to prevent overlap.

5. **Visual Connectors**: Optional lines connecting labels to their anchor points.

## Architecture

### Core Classes

#### vtkMRMLNodeLabelDisplayNode
Located in: `Libs/MRML/Core/`

The display node that stores all label configuration:
- Label text
- Anchor position mode and coordinates
- Label position in view (left/right/top/bottom)
- Line visibility
- Text scale and styling
- Reference to target node
- Segment ID (for segmentation nodes)

#### vtkMRMLNodeLabelsDisplayableManager2D
Located in: `Libs/MRML/DisplayableManager/`

Manages label display in 2D slice views:
- Creates and manages text actors and line actors
- Converts world coordinates to slice display coordinates
- Implements collision avoidance algorithm
- Handles label updates on view changes

#### vtkMRMLNodeLabelsDisplayableManager3D
Located in: `Libs/MRML/DisplayableManager/`

Manages label display in 3D views:
- Similar to 2D manager but adapted for 3D rendering
- Projects 3D anchor points to 2D display coordinates
- Manages label positioning in 3D viewport

## Usage

### Creating a Label Programmatically (C++)

```cpp
// Create a label display node
vtkNew<vtkMRMLNodeLabelDisplayNode> labelNode;
labelNode->SetName("MyNodeLabel");
scene->AddNode(labelNode);

// Configure the label
labelNode->SetLabelText("Heart Apex");
labelNode->SetAnchorPositionMode(
  vtkMRMLNodeLabelDisplayNode::AnchorPositionNodeCenter);
labelNode->SetLabelPosition(
  vtkMRMLNodeLabelDisplayNode::LabelPositionLeft);
labelNode->SetLineVisibility(true);
labelNode->SetVisibility(true);

// Attach to a target node (e.g., a segmentation)
vtkMRMLSegmentationNode* segNode = /* ... */;
labelNode->SetAndObserveTargetNodeID(segNode->GetID());

// For segmentations, specify which segment
labelNode->SetSegmentID("Segment_1");
```

### Creating a Label for a Segmentation (Python)

```python
import slicer

# Get the segmentation node
segmentationNode = slicer.util.getNode('MySegmentation')

# Create a label display node
labelNode = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLNodeLabelDisplayNode')
labelNode.SetName('LeftVentricle_Label')

# Configure the label
labelNode.SetLabelText('Left Ventricle')
labelNode.SetAnchorPositionMode(
    slicer.vtkMRMLNodeLabelDisplayNode.AnchorPositionSegmentLargestIsland)
labelNode.SetLabelPosition(slicer.vtkMRMLNodeLabelDisplayNode.LabelPositionRight)
labelNode.SetLineVisibility(True)
labelNode.SetVisibility(True)

# Attach to the segmentation and specify segment
labelNode.SetAndObserveTargetNodeID(segmentationNode.GetID())
labelNode.SetSegmentID('Segment_1')

# Customize text appearance
textProp = labelNode.GetTextProperty()
textProp.SetFontSize(24)
textProp.SetColor(1.0, 1.0, 0.0)  # Yellow
textProp.SetBold(True)
```

### Creating a Label for a Markup (Python)

```python
import slicer

# Get a markup node
markupNode = slicer.util.getNode('F')  # A fiducial list

# Create a label
labelNode = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLNodeLabelDisplayNode')
labelNode.SetName('Landmark_Label')
labelNode.SetLabelText('Anatomical Landmark')

# Use the node's center as anchor
labelNode.SetAnchorPositionMode(
    slicer.vtkMRMLNodeLabelDisplayNode.AnchorPositionNodeCenter)
labelNode.SetLabelPosition(slicer.vtkMRMLNodeLabelDisplayNode.LabelPositionTop)
labelNode.SetAndObserveTargetNodeID(markupNode.GetID())
```

## Integration Steps

To integrate this system into Slicer, the following steps are needed:

### 1. Add to MRML Core Build

Edit `Libs/MRML/Core/CMakeLists.txt`:

```cmake
# Add to MRML_SRCS
set(MRML_SRCS
  # ... existing files ...
  vtkMRMLNodeLabelDisplayNode.cxx
  vtkMRMLNodeLabelDisplayNode.h
)
```

### 2. Register the Node Type

The node will be automatically registered when added to MRML Core, but you may need to ensure it's factory-registered in `vtkMRMLScene`.

### 3. Add Displayable Managers to Build

Edit `Libs/MRML/DisplayableManager/CMakeLists.txt`:

```cmake
# Add to displayable manager sources
set(MRMLDisplayableManager_SRCS
  # ... existing files ...
  vtkMRMLNodeLabelsDisplayableManager2D.cxx
  vtkMRMLNodeLabelsDisplayableManager2D.h
  vtkMRMLNodeLabelsDisplayableManager3D.cxx
  vtkMRMLNodeLabelsDisplayableManager3D.h
)
```

### 4. Register Displayable Managers

In the application initialization code (typically in `qSlicerApplication` or a module's setup):

```cpp
#include <vtkMRMLSliceViewDisplayableManagerFactory.h>
#include <vtkMRMLThreeDViewDisplayableManagerFactory.h>

// Register 2D displayable manager
vtkMRMLSliceViewDisplayableManagerFactory::GetInstance()->
  RegisterDisplayableManager("vtkMRMLNodeLabelsDisplayableManager2D");

// Register 3D displayable manager
vtkMRMLThreeDViewDisplayableManagerFactory::GetInstance()->
  RegisterDisplayableManager("vtkMRMLNodeLabelsDisplayableManager3D");
```

### 5. Create UI Module (Optional)

Create a Slicer module to provide a user interface for creating and managing node labels:

```python
class NodeLabelsWidget:
    def setup(self):
        # Node selector to choose target node
        self.nodeSelector = slicer.qMRMLNodeComboBox()
        self.nodeSelector.nodeTypes = ["vtkMRMLDisplayableNode"]

        # Text input for label
        self.labelTextEdit = qt.QLineEdit()

        # Anchor mode combo box
        self.anchorModeCombo = qt.QComboBox()
        self.anchorModeCombo.addItem("Manual")
        self.anchorModeCombo.addItem("Node Center")
        self.anchorModeCombo.addItem("Segment Largest Island")

        # Label position combo box
        self.positionCombo = qt.QComboBox()
        self.positionCombo.addItem("Default")
        self.positionCombo.addItem("Left")
        self.positionCombo.addItem("Right")
        self.positionCombo.addItem("Top")
        self.positionCombo.addItem("Bottom")

        # Create button
        self.createButton = qt.QPushButton("Create Label")
        self.createButton.connect('clicked()', self.onCreateLabel)
```

## Advanced Features

### Collision Avoidance Algorithm

The collision avoidance system works by:

1. Grouping labels by their position preference (left/right/top/bottom)
2. Within each group, detecting overlapping bounding boxes
3. Applying jitter offsets to separate overlapping labels
4. The jitter is calculated based on the order of labels in the scene

This ensures that all labels remain readable even when multiple nodes have labels in the same region.

### Extending for Custom Anchor Calculations

To add custom anchor position calculations:

1. Add a new enum value to `AnchorPositionModeType` in `vtkMRMLNodeLabelDisplayNode.h`
2. Implement the calculation in the displayable manager's `CalculateAnchorPosition` method
3. Update the string conversion methods for the new mode

Example for a custom "NodeTop" mode:

```cpp
case vtkMRMLNodeLabelDisplayNode::AnchorPositionNodeTop:
{
  double bounds[6];
  targetNode->GetRASBounds(bounds);
  anchorPos[0] = (bounds[0] + bounds[1]) / 2.0;
  anchorPos[1] = (bounds[2] + bounds[3]) / 2.0;
  anchorPos[2] = bounds[5]; // Top of bounds
  return true;
}
```

### Segment Largest Island Implementation

The current implementation uses `GetSegmentCenterRAS()` which returns the overall segment center. To implement true "largest island on slice" functionality:

1. Get the segment's representation on the current slice
2. Use connected components analysis to identify islands
3. Calculate the center of mass of the largest island
4. This would require integration with `vtkSegmentation` and slice-specific geometry

## Comparison with Markups Labels

### Advantages of the New System

1. **Universal**: Works with any node type, not just Markups
2. **Centralized**: All labels managed by a single displayable manager
3. **Collision-aware**: Automatic adjustment to prevent overlap
4. **Flexible**: Multiple anchor and position modes
5. **Extensible**: Easy to add new positioning algorithms

### Migration Path

Existing Markups label positioning can coexist with this system:
- Markups can continue to use their built-in labels for control points
- This system provides additional "property labels" for the entire markup
- Future work could consolidate both systems

## Future Enhancements

1. **Smart Positioning**: Machine learning-based optimal label placement
2. **Label Clustering**: Group related labels together
3. **Interactive Dragging**: Allow users to manually adjust label positions
4. **Persistence**: Save label positions with scenes
5. **Animation**: Smooth transitions when labels move
6. **Leader Lines**: Curved or angled connector lines
7. **Multi-line Text**: Support for formatted, multi-line labels
8. **Icons**: Support for icon/symbol labels in addition to text

## Performance Considerations

- Labels are only updated when:
  - The view is modified (camera moved, zoom changed)
  - The target node is modified
  - The label display node properties change
- Collision detection uses simple bounding box checks
- For scenes with many labels (>100), consider:
  - Spatial indexing for collision detection
  - LOD (Level of Detail) system to hide distant labels
  - Culling labels outside the view frustum

## Testing

### Test Scenarios

1. **Basic Label Creation**: Create labels for various node types
2. **Position Modes**: Test all label positions (left/right/top/bottom)
3. **Anchor Modes**: Test all anchor calculation modes
4. **Collision**: Create multiple labels with same position preference
5. **Segmentation**: Test with segmentation nodes and specific segments
6. **View Changes**: Rotate, zoom, pan - labels should update correctly
7. **Node Deletion**: Deleting target node should handle gracefully
8. **Multiple Views**: Labels should appear correctly in multiple slice/3D views

### Unit Tests

```cpp
// Example test structure
int TestNodeLabelDisplayNode(int argc, char* argv[])
{
  vtkNew<vtkMRMLScene> scene;

  // Test label creation
  vtkNew<vtkMRMLNodeLabelDisplayNode> label;
  scene->AddNode(label);

  // Test property setters/getters
  label->SetLabelText("Test");
  if (strcmp(label->GetLabelText(), "Test") != 0)
  {
    return EXIT_FAILURE;
  }

  // Test anchor mode conversion
  label->SetAnchorPositionMode(
    vtkMRMLNodeLabelDisplayNode::AnchorPositionNodeCenter);
  const char* modeStr = label->GetAnchorPositionModeAsString(
    label->GetAnchorPositionMode());
  // ... more tests

  return EXIT_SUCCESS;
}
```

## Troubleshooting

### Labels Not Appearing

1. Check label visibility: `labelNode->GetVisibility()` should return true
2. Check target node is set: `labelNode->GetTargetNode()` should not be nullptr
3. Verify displayable managers are registered
4. Check renderer is valid in displayable manager

### Labels in Wrong Position

1. Verify anchor calculation is correct
2. Check coordinate transformations (world to display)
3. Ensure slice/view node is accessible

### Build Errors

1. Ensure all header files are in the CMakeLists.txt
2. Check for missing VTK/MRML includes
3. Verify export macros are correct

## License

This code follows the same BSD-style license as 3D Slicer.

## Contributing

To extend this system:
1. Follow Slicer coding conventions
2. Add unit tests for new features
3. Update this documentation
4. Submit pull request with clear description

## Contact

For questions or issues with this system, please file an issue on the Slicer GitHub repository or post on the Slicer forum.
