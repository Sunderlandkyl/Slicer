/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Brigham and Women's Hospital

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

#ifndef __vtkMRMLLabelsDisplayableManager_h
#define __vtkMRMLLabelsDisplayableManager_h

// MRMLDisplayableManager includes
#include "vtkMRMLAbstractDisplayableManager.h"
#include "vtkMRMLDisplayableManagerExport.h"

// Forward declarations
class vtkMRMLLabelDisplayNode;
class vtkMRMLLabelsWidget;

/// \brief Base class for displaying node labels in views
///
/// Displays text labels for any MRML node at specified anchor positions.
/// Handles automatic collision avoidance to prevent label overlap.
/// Subclasses implement view-specific functionality (2D slice vs 3D).
///
class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLLabelsDisplayableManager
  : public vtkMRMLAbstractDisplayableManager
{

public:
  vtkTypeMacro(vtkMRMLLabelsDisplayableManager, vtkMRMLAbstractDisplayableManager);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /// Called from RequestRender() path when UpdateFromMRML was requested
  void UpdateFromMRML() override;

  /// Update labels when renderer/view changes
  void UpdateFromRenderer();

protected:
  vtkMRMLLabelsDisplayableManager();
  ~vtkMRMLLabelsDisplayableManager() override;

  void UnobserveMRMLScene() override;
  void OnMRMLSceneNodeAdded(vtkMRMLNode* node) override;
  void OnMRMLSceneNodeRemoved(vtkMRMLNode* node) override;
  void ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* callData) override;

  /// Initialize the displayable manager
  void Create() override;

  /// Called each time the view node is modified.
  /// Internally update the renderer from the view node.
  void OnMRMLDisplayableNodeModifiedEvent(vtkObject* caller) override;

  /// Method to perform additional initialization
  void AdditionalInitializeStep() override;

  /// Create a widget for a label display node (subclasses override)
  virtual vtkMRMLLabelsWidget* CreateWidget(vtkMRMLLabelDisplayNode* displayNode) = 0;

  /// Get widget for a display node
  vtkMRMLLabelsWidget* GetWidget(vtkMRMLLabelDisplayNode* displayNode);

  /// Add a widget
  void AddWidget(vtkMRMLLabelDisplayNode* displayNode);

  /// Remove a widget
  void RemoveWidget(vtkMRMLLabelDisplayNode* displayNode);

  /// Remove all widgets
  void RemoveAllWidgets();

private:
  vtkMRMLLabelsDisplayableManager(const vtkMRMLLabelsDisplayableManager&) = delete;
  void operator=(const vtkMRMLLabelsDisplayableManager&) = delete;

  class vtkInternal;
  vtkInternal* Internal;
  friend class vtkInternal;
};

#endif
