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
  Care Ontario, OpenAnatomy, and Brigham and Women’s Hospital through NIH grant R01MH112748.

==============================================================================*/

#ifndef __vtkMRMLInteractionDisplayableManager_h
#define __vtkMRMLInteractionDisplayableManager_h

// MRMLDisplayManager includes
#include <vtkMRMLDisplayableManagerExport.h>

#include <vtkMRMLInteractionWidget.h>

// MRMLDisplayableManager includes
#include <vtkMRMLAbstractDisplayableManager.h>

// STD includes
#include <map>

#include <vtkMRMLSliceNode.h>
#include <vtkWeakPointer.h>

class vtkMRMLDisplayNode;
class vtkMRMLDisplayableNode;

class  VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLInteractionDisplayableManager :
    public vtkMRMLAbstractDisplayableManager
{
public:

  // Allow the helper to call protected methods of displayable manager
  friend class vtkMRMLInteractionDisplayableManagerHelper;

  static vtkMRMLInteractionDisplayableManager *New();
  vtkTypeMacro(vtkMRMLInteractionDisplayableManager, vtkMRMLAbstractDisplayableManager);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /// Check if this is a 2d SliceView displayable manager, returns true if so,
  /// false otherwise. Checks return from GetSliceNode for non null, which means
  /// it's a 2d displayable manager
  virtual bool Is2DDisplayableManager();
  /// Get the sliceNode, if registered. This would mean it is a 2D SliceView displayableManager.
  vtkMRMLSliceNode * GetMRMLSliceNode();

  vtkMRMLInteractionWidget* FindClosestWidget(vtkMRMLInteractionEventData* callData, double& closestDistance2);

  bool CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double &closestDistance2) override;
  bool ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData) override;

  void SetHasFocus(bool hasFocus) override;
  bool GetGrabFocus() override;
  bool GetInteractive() override;
  int GetMouseCursor() override;

  int GetCurrentInteractionMode();

  // Methods from vtkMRMLAbstractSliceViewDisplayableManager

  /// Convert device coordinates (display) to XYZ coordinates (viewport).
  /// Parameter \a xyz is double[3]
  /// \sa ConvertDeviceToXYZ(vtkRenderWindowInteractor *, vtkMRMLSliceNode *, double x, double y, double xyz[3])
  void ConvertDeviceToXYZ(double x, double y, double xyz[3]);

protected:

  vtkMRMLInteractionDisplayableManager();
  ~vtkMRMLInteractionDisplayableManager() override;

  void ProcessMRMLNodesEvents(vtkObject *caller, unsigned long event, void *callData) override;

  vtkMRMLInteractionWidget* CreateWidget(vtkMRMLDisplayNode* node);

  /// Wrap the superclass render request in a check for batch processing
  virtual void RequestRender();

  void SetMRMLSceneInternal(vtkMRMLScene* newScene) override;

  /// Called after the corresponding MRML event is triggered, from AbstractDisplayableManager
  /// \sa ProcessMRMLSceneEvents
  void UpdateFromMRMLScene() override;
  void OnMRMLSceneEndClose() override;
  void OnMRMLSceneEndImport() override;
  void OnMRMLSceneNodeAdded(vtkMRMLNode* node) override;
  void OnMRMLSceneNodeRemoved(vtkMRMLNode* node) override;

  /// Called after the corresponding MRML View container was modified
  void OnMRMLDisplayableNodeModifiedEvent(vtkObject* caller) override;

  /// Handler for specific SliceView actions, iterate over the widgets in the helper
  virtual void OnMRMLSliceNodeModifiedEvent();

  /// Check if it is the right displayManager
  virtual bool IsCorrectDisplayableManager();

  /// Return true if this displayable manager supports(can manage) that node,
  /// false otherwise.
  /// Can be reimplemented to add more conditions.
  /// \sa IsManageable(const char*), IsCorrectDisplayableManager()
  virtual bool IsManageable(vtkMRMLNode* node);
  /// Return true if this displayable manager supports(can manage) that node class,
  /// false otherwise.
  /// Can be reimplemented to add more conditions.
  /// \sa IsManageable(vtkMRMLNode*), IsCorrectDisplayableManager()
  virtual bool IsManageable(const char* nodeClassName);

  /// Respond to interactor style events
  void OnInteractorStyleEvent(int eventid) override;

  /// Accessor for internal flag that disables interactor style event processing
  vtkGetMacro(DisableInteractorStyleEventsProcessing, int);

  std::set<vtkSmartPointer<vtkMRMLDisplayableNode>> DisplayableNodes;
  std::set<vtkSmartPointer<vtkMRMLDisplayNode>> DisplayNodes;
  std::map<vtkSmartPointer<vtkMRMLDisplayNode>, vtkSmartPointer<vtkMRMLInteractionWidget>> InteractionWidgets;

  vtkSmartPointer<vtkIdTypeArray> DisplayableEvents;

  void AddDisplayNode(vtkMRMLDisplayNode* displayNode);
  void RemoveDisplayNode(vtkMRMLDisplayNode* displayableNode);
  void RemoveAllDisplayNodes();

  void AddDisplayableNode(vtkMRMLDisplayableNode* displayableNode);
  void RemoveDisplayableNode(vtkMRMLDisplayableNode* displayableNode);
  void RemoveAllDisplayableNodes();

  void AddObservations(vtkMRMLDisplayableNode* displayableNode);
  void RemoveObservations(vtkMRMLDisplayableNode* displayableNode);

private:
  vtkMRMLInteractionDisplayableManager(const vtkMRMLInteractionDisplayableManager&) = delete;
  void operator=(const vtkMRMLInteractionDisplayableManager&) = delete;

  int DisableInteractorStyleEventsProcessing;

  // by default, this displayableManager handles a 2d view, so the SliceNode
  // must be set when it's assigned to a viewer
  vtkWeakPointer<vtkMRMLSliceNode> SliceNode;

  vtkWeakPointer<vtkMRMLInteractionWidget> LastActiveWidget;
};

#endif
