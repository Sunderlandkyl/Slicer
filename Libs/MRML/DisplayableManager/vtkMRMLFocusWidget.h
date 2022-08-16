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

/**
 * @class   vtkMRMLFocusWidget
 * @brief   Display focus visualizations and process events.
 *
 * Widget used to control interaction events involving node focus.
*/

#ifndef vtkMRMLFocusWidget_h
#define vtkMRMLFocusWidget_h

// MRMLDM includes
#include "vtkMRMLDisplayableManagerExport.h" // For export macro
#include "vtkMRMLAbstractWidget.h"

class vtkMRMLSelectionNode;

class vtkObserverManager;
class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLFocusWidget : public vtkMRMLAbstractWidget
{
public:
  ///@{
  /// Instantiate this class.
  static vtkMRMLFocusWidget *New();
  ///@}

  ///@{
  /// Standard VTK class macros.
  vtkTypeMacro(vtkMRMLFocusWidget, vtkMRMLAbstractWidget);
  void PrintSelf(ostream& os, vtkIndent indent) override;
  ///@}

  /// Set/Get the current selection node.
  void SetSelectionNode(vtkMRMLSelectionNode* selectionNode);
  vtkMRMLSelectionNode* GetSelectionNode();

  /// Return true if the widget can process the event.
  bool CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double &distance2) override;

  /// Process interaction event.
  bool ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData) override;
  virtual bool ProcessCancelFocusEvent(vtkMRMLInteractionEventData* eventData);

  /// Widget states
  enum
  {
    WidgetStateFocus = WidgetStateUser, ///< A node currently has focus.
  };

  /// Widget events
  enum
  {
    WidgetEventCancelFocus = WidgetEventUser,
  };

protected:
  vtkMRMLFocusWidget();
  ~vtkMRMLFocusWidget() override;

  static void ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* clientData, void* callData);

  vtkGetObjectMacro(MRMLNodesObserverManager, vtkObserverManager);

protected:
  vtkNew<vtkObserverManager> MRMLNodesObserverManager;

  vtkMRMLSelectionNode* SelectionNode{ nullptr };

private:
  vtkMRMLFocusWidget(const vtkMRMLFocusWidget&) = delete;
  void operator=(const vtkMRMLFocusWidget&) = delete;
};

#endif
