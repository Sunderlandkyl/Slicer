/*==============================================================================

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Kyle Sunderland, PerkLab, Queen's University
  and was supported through CANARIE's Research Software Program, Cancer
  Care Ontario, OpenAnatomy, and Brigham and Women's Hospital through NIH grant R01MH112748.

==============================================================================*/

#ifndef __vtkMRMLFocusDisplayableManager_h
#define __vtkMRMLFocusDisplayableManager_h

// MRMLDisplayableManager includes
#include "vtkMRMLAbstractDisplayableManager.h"
#include "vtkMRMLDisplayableManagerExport.h"

class vtkProp;

/// \brief Manage display nodes with polydata in 3D views.
///
/// Any display node in the scene that contains a valid output polydata is
/// represented into the view renderer using configured synchronized vtkActors
/// and vtkMappers.
/// Note that the display nodes must be of type vtkMRMLModelDisplayNode
/// (to have an output polydata) but the displayable nodes don't necessarily
/// have to be of type vtkMRMLModelNode.
class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLFocusDisplayableManager
  : public vtkMRMLAbstractDisplayableManager
{
public:
  static vtkMRMLFocusDisplayableManager* New();
  vtkTypeMacro(vtkMRMLFocusDisplayableManager, vtkMRMLAbstractDisplayableManager);
  void PrintSelf(ostream& os, vtkIndent indent) override;

protected:
  // TODO
  void UpdateSelectionNode();

  // TODO
  void SetAndObserveSelectionNode(vtkMRMLSelectionNode* newSelectionNode);

  // Update selection node observer
  void UpdateFromMRMLScene() override;

  /// Updates Actors based on models in the scene
  void UpdateFromMRML() override;

  void UpdateDisplayableNodes();
  void UpdateOriginalFocusActors();

  void UpdateSoftFocus();

  void UpdateHardFocus();

  void ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* callData) override;

  bool CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double& closestDistance2) override;
  bool ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData) override;

  void UpdateActors();
  void UpdateActor(vtkProp* originalProp);

  void UpdateActorRASBounds();
  void UpdateCornerROIPolyData();

  vtkMRMLNode* GetFocusNode();

protected:
  vtkMRMLFocusDisplayableManager();
  ~vtkMRMLFocusDisplayableManager() override;

private:
  vtkMRMLFocusDisplayableManager(const vtkMRMLFocusDisplayableManager&) = delete;
  void operator=(const vtkMRMLFocusDisplayableManager&) = delete;

  class vtkInternal;
  vtkInternal* Internal;
};

#endif
