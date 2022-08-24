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

/// \brief Manage display of nodes that are currently focused.
///
/// Any display node in the scene that contains a valid actor will be used to
/// generate the focus actors.
/// The currently focused node is controlled by vtkMRMLSelectionNode.
class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLFocusDisplayableManager
  : public vtkMRMLAbstractDisplayableManager
{
public:
  static vtkMRMLFocusDisplayableManager* New();
  vtkTypeMacro(vtkMRMLFocusDisplayableManager, vtkMRMLAbstractDisplayableManager);
  void PrintSelf(ostream& os, vtkIndent indent) override;

protected:

  ///@{
  // Update selection node observer
  void UpdateFromMRMLScene() override;
  ///@}

  ///@{
  /// Perform a full update for all actors and nodes.
  void UpdateFromMRML() override;
  ///@}

  ///@{
  /// Perform a full update for all actors and nodes.
  virtual void UpdateFromDisplayableNode();
  ///@}

  ///@{
  /// Update the list of displayable nodes that have focus.
  void UpdateHardFocusDisplayableNodes();
  ///@}

  ///@{
  /// Update the list of displayable nodes that have focus.
  void UpdateSoftFocusDisplayableNodes();
  ///@}

  ///@{
  /// Update the list of actors for focused nodes.
  void UpdateOriginalHardFocusActors();
  ///@}


  ///@{
  /// Update the list of actors for focused nodes.
  void UpdateOriginalSoftFocusActors();
  ///@}


  ///@{
  /// Update actors for the soft focus representation.
  void UpdateSoftFocus();
  ///@}

  ///@{
  /// Update actors for the hard focus representation.
  void UpdateHardFocus();
  ///@}

  ///@{
  /// Handle events from observed nodes/actors/etc.
  void ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* callData) override;
  ///@}

  ///@{
  /// Handle events from observed nodes/actors/etc.
  virtual void ProcesObjectsEvents(vtkObject* caller, unsigned long event, void* callData);
  ///@}

  ///@{
  /// Return true if the widget can process the event.
  bool CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double& closestDistance2) override;
  ///@}

  ///@{
  /// Process interaction event.
  bool ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData) override;
  ///@}

  ///@{
  /// Update the appearance of the derived focus actor based on the original actor.
  void UpdateActors();
  void UpdateActor(vtkProp* originalProp);
  ///@}

  ///@{
  /// Get the bounds of the focused actors.
  void UpdateActorRASBounds();
  ///@}

  ///@{
  /// Update the polydata drawing the hard focus box.
  void UpdateCornerROIPolyData();
  ///@}

  ///@{
  /// Returns the currently focused node if one is set, otherwise returns nullptr.
  vtkMRMLNode* GetFocusNode();
  ///@}

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
