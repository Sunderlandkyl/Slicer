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

#ifndef __vtkMRMLNodeLabelsDisplayableManager2D_h
#define __vtkMRMLNodeLabelsDisplayableManager2D_h

// MRMLDisplayableManager includes
#include "vtkMRMLAbstractSliceViewDisplayableManager.h"

#include "vtkMRMLDisplayableManagerExport.h"

class vtkMRMLLabelDisplayNode;

/// \brief Displayable manager for showing node labels in slice (2D) views.
///
/// Displays text labels for any MRML node at specified anchor positions in slice viewers.
/// Handles automatic collision avoidance to prevent label overlap.
///
class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLNodeLabelsDisplayableManager2D
  : public vtkMRMLAbstractSliceViewDisplayableManager
{

public:
  static vtkMRMLNodeLabelsDisplayableManager2D* New();
  vtkTypeMacro(vtkMRMLNodeLabelsDisplayableManager2D, vtkMRMLAbstractSliceViewDisplayableManager);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /// Called from RequestRender() path when UpdateFromMRML was requested
  void UpdateFromMRML() override;

  /// Update labels when renderer/view changes
  void UpdateFromRenderer();

protected:
  vtkMRMLNodeLabelsDisplayableManager2D();
  ~vtkMRMLNodeLabelsDisplayableManager2D() override;

  void UnobserveMRMLScene() override;
  void OnMRMLSceneNodeAdded(vtkMRMLNode* node) override;
  void OnMRMLSceneNodeRemoved(vtkMRMLNode* node) override;
  void ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* callData) override;

  /// Initialize the displayable manager
  void Create() override;

  /// Called each time the view node is modified.
  /// Internally update the renderer from the view node.
  void OnMRMLDisplayableNodeModifiedEvent(vtkObject* caller) override;

  /// Called when the slice node is modified (slice offset, zoom, etc.)
  void OnMRMLSliceNodeModifiedEvent() override;

  /// Method to perform additional initialization
  void AdditionalInitializeStep() override;

private:
  vtkMRMLNodeLabelsDisplayableManager2D(const vtkMRMLNodeLabelsDisplayableManager2D&) = delete;
  void operator=(const vtkMRMLNodeLabelsDisplayableManager2D&) = delete;

  class vtkInternal;
  vtkInternal* Internal;
  friend class vtkInternal;
};

#endif
