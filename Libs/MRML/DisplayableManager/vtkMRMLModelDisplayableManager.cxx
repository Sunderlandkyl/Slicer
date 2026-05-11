/*=========================================================================

  Copyright 2005 Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

==========================================================================*/

// MRMLDisplayableManager includes
#include "vtkMRMLModelDisplayableManager.h"
#include "vtkMRMLThreeDViewInteractorStyle.h"
#include "vtkMRMLApplicationLogic.h"

// MRML/Slicer includes
#include <vtkEventBroker.h>
#include <vtkMRMLClipNode.h>
#include <vtkMRMLColorNode.h>
#include <vtkMRMLDisplayNode.h>
#include <vtkMRMLDisplayableNode.h>
#include <vtkMRMLFolderDisplayNode.h>
#include <vtkMRMLInteractionNode.h>
#include <vtkMRMLModelDisplayNode.h>
#include <vtkMRMLModelNode.h>
#include <vtkMRMLScene.h>
#include <vtkMRMLSelectionNode.h>
#include <vtkMRMLSliceLogic.h>
#include <vtkMRMLSliceNode.h>
#include <vtkMRMLSubjectHierarchyConstants.h>
#include <vtkMRMLSubjectHierarchyNode.h>
#include <vtkMRMLTransformNode.h>
#include <vtkMRMLViewNode.h>
#include <vtkMRMLVolumeNode.h>

// VTK includes
#include <vtkAlgorithm.h>
#include <vtkAlgorithmOutput.h>
#include <vtkAssignAttribute.h>
#include <vtkCapPolyData.h>
#include <vtkCallbackCommand.h>
#include <vtkCellArray.h>
#include <vtkClipDataSet.h>
#include <vtkClipPolyData.h>
#include <vtkColorTransferFunction.h>
#include <vtkDataSetAttributes.h>
#include <vtkDataSetMapper.h>
#include <vtkExtractCells.h>
#include <vtkExtractGeometry.h>
#include <vtkExtractPolyDataGeometry.h>
#include <vtkGeneralTransform.h>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImageMapper3D.h>
#include <vtkImplicitBoolean.h>
#include <vtkImplicitFunction.h>
#include <vtkImplicitFunctionCollection.h>
#include <vtkLookupTable.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPlane.h>
#include <vtkPlaneCollection.h>
#include <vtkPointData.h>
#include <vtkPointSet.h>
#include <vtkPolyDataMapper.h>
#include <vtkProp3DCollection.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSmartPointer.h>
#include <vtkTexture.h>
#include <vtkTransform.h>
#include <vtkTransformFilter.h>
#include <vtkVersion.h>
#include <vtkWeakPointer.h>
// For picking
#include <vtkCellPicker.h>
#include <vtkPointPicker.h>
#include <vtkPropPicker.h>
#include <vtkRendererCollection.h>
#include <vtkWorldPointPicker.h>

//---------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLModelDisplayableManager);

//---------------------------------------------------------------------------
class vtkMRMLModelDisplayableManager::vtkInternal
{
public:
  vtkInternal(vtkMRMLModelDisplayableManager* external);
  ~vtkInternal();

  /// Reset all the pick vars
  void ResetPick();
  /// Find picked node from mesh and set PickedNodeID in Internal
  void FindPickedDisplayNodeFromMesh(vtkPointSet* mesh, double pickedPoint[3]);
  /// Find node in scene from imageData and set PickedNodeID in Internal
  void FindDisplayNodeFromImageData(vtkMRMLScene* scene, vtkImageData* imageData);
  /// Find picked point index in mesh and picked cell (PickedCellID) and set PickedPointID in Internal
  void FindPickedPointOnMeshAndCell(vtkPointSet* mesh, double pickedPoint[3]);
  /// Find first picked node from prop3Ds in cell picker and set PickedNodeID in Internal
  void FindFirstPickedDisplayNodeFromPickerProp3Ds();

  struct ModelDisplayPipeline
  {
    vtkWeakPointer<vtkMRMLModelDisplayNode> DisplayNode;

    vtkSmartPointer<vtkProp3D> Actor;
    vtkSmartPointer<vtkTransformFilter> TransformFilter;

    int ClipState;
    vtkSmartPointer<vtkAlgorithm> Clipper;
    vtkSmartPointer<vtkCapPolyData> Capper;
    vtkSmartPointer<vtkProp3D> CapActor;
  };
  ModelDisplayPipeline* GetDisplayPipeline(const std::string& displayNodeID)
  {
    auto iter = this->DisplayPipelines.find(displayNodeID);
    if (iter != this->DisplayPipelines.end())
    {
      return &iter->second;
    }
    return nullptr;
  }

public:
  vtkMRMLModelDisplayableManager* External;

  // Key is the display node ID
  std::map<std::string, ModelDisplayPipeline> DisplayPipelines;

  std::set<vtkWeakPointer<vtkMRMLModelNode>> ObservedModelNodes;

  bool IsUpdatingModelsFromMRML;

  // clang-format off
  vtkSmartPointer<vtkWorldPointPicker> WorldPointPicker;
  vtkSmartPointer<vtkPropPicker>       PropPicker;
  vtkSmartPointer<vtkCellPicker>       CellPicker;
  vtkSmartPointer<vtkPointPicker>      PointPicker;
  // clang-format on

  // Information about a pick event
  // clang-format off
  std::string  PickedDisplayNodeID;
  double       PickedRAS[3];
  vtkIdType    PickedCellID;
  vtkIdType    PickedPointID;
  // clang-format on

  // Used for caching the node pointer so that we do not have to search in the scene each time.
  // We do not add an observer therefore we can let the selection node deleted without our knowledge.
  vtkWeakPointer<vtkMRMLSelectionNode> SelectionNode;

  std::vector<int> ModelNodeEvents;
};

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLModelDisplayableManager::vtkInternal::vtkInternal(vtkMRMLModelDisplayableManager* external)
  : External(external)
{
  // Instantiate and initialize Pickers
  this->WorldPointPicker = vtkSmartPointer<vtkWorldPointPicker>::New();
  this->PropPicker = vtkSmartPointer<vtkPropPicker>::New();
  this->CellPicker = vtkSmartPointer<vtkCellPicker>::New();
  this->CellPicker->SetTolerance(0.00001);
  this->PointPicker = vtkSmartPointer<vtkPointPicker>::New();
  this->ResetPick();

  this->IsUpdatingModelsFromMRML = false;

  this->ModelNodeEvents = {
    vtkMRMLModelNode::MeshModifiedEvent, vtkMRMLDisplayableNode::DisplayModifiedEvent, vtkMRMLTransformableNode::TransformModifiedEvent, vtkMRMLClipNode::ClipNodeModifiedEvent
  };
}

//---------------------------------------------------------------------------
vtkMRMLModelDisplayableManager::vtkInternal::~vtkInternal() = default;

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::vtkInternal::ResetPick()
{
  this->PickedDisplayNodeID.clear();
  for (int i = 0; i < 3; i++)
  {
    this->PickedRAS[i] = 0.0;
  }
  this->PickedCellID = -1;
  this->PickedPointID = -1;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::vtkInternal::FindPickedDisplayNodeFromMesh(vtkPointSet* mesh, double vtkNotUsed(pickedPoint)[3])
{
  if (!mesh)
  {
    return;
  }

  for (auto modelIt = this->DisplayPipelines.begin(); modelIt != this->DisplayPipelines.end(); modelIt++)
  {
    ModelDisplayPipeline* pipeline = &modelIt->second;
    vtkMRMLModelDisplayNode* modelNode = pipeline->DisplayNode;
    if (modelNode && modelNode->GetOutputMesh() == mesh)
    {
      this->PickedDisplayNodeID = modelIt->first;
      return; // Display node found
    }
  }
}

//
//---------------------------------------------------------------------------
// for consistency with other vtkInternal classes this does not have access
// to the mrmlScene, so it is passed as a parameter
void vtkMRMLModelDisplayableManager::vtkInternal::FindDisplayNodeFromImageData(vtkMRMLScene* scene, vtkImageData* imageData)
{
  if (!scene || !imageData)
  {
    return;
  }
  // note that this library doesn't link to the VolumeRendering code because it is
  // a loadable module.  However we can still iterate over volume rendering nodes
  // and use the superclass abstract methods to confirm that the passed in imageData
  // corresponds to the display node.
  std::vector<vtkMRMLNode*> displayNodes;
  int nodeCount = scene->GetNodesByClass("vtkMRMLVolumeRenderingDisplayNode", displayNodes);
  for (int nodeIndex = 0; nodeIndex < nodeCount; nodeIndex++)
  {
    vtkMRMLDisplayNode* displayNode = vtkMRMLDisplayNode::SafeDownCast(displayNodes[nodeIndex]);
    if (displayNode)
    {
      vtkMRMLVolumeNode* volumeNode = vtkMRMLVolumeNode::SafeDownCast(displayNode->GetDisplayableNode());
      vtkImageData* volumeImageData = nullptr;
      if (volumeNode)
      {
        volumeImageData = vtkImageData::SafeDownCast(volumeNode->GetImageData());
      }
      if (volumeImageData && volumeImageData == imageData)
      {
        this->PickedDisplayNodeID = displayNode->GetID();
        return; // Display node found
      }
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::vtkInternal::FindPickedPointOnMeshAndCell(vtkPointSet* mesh, double pickedPoint[3])
{
  if (!mesh || this->PickedCellID < 0)
  {
    return;
  }

  // Figure out the closest vertex in the picked cell to the picked RAS
  // point. Only doing this on model nodes for now.
  vtkCell* cell = mesh->GetCell(this->PickedCellID);
  if (!cell)
  {
    return;
  }

  int numPoints = cell->GetNumberOfPoints();
  int closestPointId = -1;
  double closestDistance = 0.0l;
  for (int p = 0; p < numPoints; p++)
  {
    int pointId = cell->GetPointId(p);
    double* pointCoords = mesh->GetPoint(pointId);
    if (pointCoords != nullptr)
    {
      double distance = sqrt(pow(pointCoords[0] - pickedPoint[0], 2) + //
                             pow(pointCoords[1] - pickedPoint[1], 2) + //
                             pow(pointCoords[2] - pickedPoint[2], 2));
      if (p == 0 || distance < closestDistance)
      {
        closestDistance = distance;
        closestPointId = pointId;
      }
    }
  }
  this->PickedPointID = closestPointId;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::vtkInternal::FindFirstPickedDisplayNodeFromPickerProp3Ds()
{
  if (!this->CellPicker)
  {
    return;
  }

  vtkProp3DCollection* props = this->CellPicker->GetProp3Ds();
  for (int propIndex = 0; propIndex < props->GetNumberOfItems(); ++propIndex)
  {
    vtkProp3D* pickedProp = vtkProp3D::SafeDownCast(props->GetItemAsObject(propIndex));
    if (!pickedProp)
    {
      continue;
    }
    for (auto propIt = this->DisplayPipelines.begin(); propIt != this->DisplayPipelines.end(); propIt++)
    {
      if (pickedProp == propIt->second.Actor)
      {
        this->PickedDisplayNodeID = propIt->first;
        return; // Display node found
      }
    }
  }
}

//---------------------------------------------------------------------------
// vtkMRMLModelDisplayableManager methods

//---------------------------------------------------------------------------
vtkMRMLModelDisplayableManager::vtkMRMLModelDisplayableManager()
{
  this->Internal = new vtkInternal(this);
}

//---------------------------------------------------------------------------
vtkMRMLModelDisplayableManager::~vtkMRMLModelDisplayableManager()
{
  this->Internal->SelectionNode = nullptr; // WeakPointer, therefore must not use vtkSetMRMLNodeMacro
  this->RemoveAllDisplayPipelines();
  delete this->Internal;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::PrintSelf(ostream& os, vtkIndent indent)
{
  this->vtkObject::PrintSelf(os, indent);

  os << indent << "vtkMRMLModelDisplayableManager: " << this->GetClassName() << "\n";

  os << indent << "PickedDisplayNodeID = " << this->Internal->PickedDisplayNodeID.c_str() << "\n";
  os << indent << "PickedRAS = (" << this->Internal->PickedRAS[0] << ", " << this->Internal->PickedRAS[1] << ", " << this->Internal->PickedRAS[2] << ")\n";
  os << indent << "PickedCellID = " << this->Internal->PickedCellID << "\n";
  os << indent << "PickedPointID = " << this->Internal->PickedPointID << "\n";
}

//---------------------------------------------------------------------------
int vtkMRMLModelDisplayableManager::ActiveInteractionModes()
{
  // return vtkMRMLInteractionNode::ViewTransform;
  return 0;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* callData)
{
  if (this->GetMRMLScene() == nullptr)
  {
    return;
  }
  if (this->GetInteractor() &&                    //
      this->GetInteractor()->GetRenderWindow() && //
      this->GetInteractor()->GetRenderWindow()->CheckInRenderStatus())
  {
    vtkDebugMacro("skipping ProcessMRMLNodesEvents during render");
    return;
  }

  if (vtkMRMLDisplayableNode::SafeDownCast(caller))
  {
    // There is no need to request a render (which can be expensive if the
    // volume rendering is on) if nothing visible has changed.
    bool requestRender = true;
    vtkMRMLModelNode* modelNode = vtkMRMLModelNode::SafeDownCast(caller);
    vtkMRMLModelDisplayNode* displayNode = reinterpret_cast<vtkMRMLModelDisplayNode*>(callData);
    switch (event)
    {
      case vtkMRMLDisplayableNode::DisplayModifiedEvent:
        // don't go any further if the modified display node is not a model
        if (!this->IsModelDisplayable(modelNode) && //
            !this->IsModelDisplayable(displayNode))
        {
          requestRender = false;
          break;
        } // else fall through
      case vtkCommand::ModifiedEvent:
      case vtkMRMLModelNode::MeshModifiedEvent:
      case vtkMRMLTransformableNode::TransformModifiedEvent:
      case vtkMRMLClipNode::ClipNodeModifiedEvent: //
        requestRender = this->OnMRMLDisplayableModelNodeModifiedEvent(modelNode);
        break;
      default:
        // We don't expect any other types of events.
        break;
    }
    if (requestRender)
    {
      this->RequestRender();
    }
  }
  else
  {
    this->Superclass::ProcessMRMLNodesEvents(caller, event, callData);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::UnobserveMRMLScene()
{
  this->RemoveAllDisplayPipelines();
  this->RemoveModelObservers();
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::OnMRMLSceneStartClose()
{
  this->RemoveModelObservers();
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::OnMRMLSceneEndClose()
{
  // Clean
  this->RemoveAllDisplayPipelines();
  this->RemoveModelObservers();

  this->SetUpdateFromMRMLRequested(true);
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::OnMRMLSceneEndBatchProcess()
{
  this->PruneMissingNodes();
  this->SetUpdateFromMRMLRequested(true);
  Superclass::OnMRMLSceneEndBatchProcess();
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::UpdateFromMRMLScene()
{
  // UpdateFromMRML will be executed only if there has been some actions
  // during the import that requested it (don't call
  // SetUpdateFromMRMLRequested(1) here, it should be done somewhere else
  // maybe in OnMRMLSceneNodeAddedEvent, OnMRMLSceneNodeRemovedEvent or
  // OnMRMLDisplayableModelNodeModifiedEvent).
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
{
  if (!node->IsA("vtkMRMLDisplayableNode") //
      && !node->IsA("vtkMRMLDisplayNode"))
  {
    return;
  }

  this->SetUpdateFromMRMLRequested(true);

  // Escape if the scene a scene is being closed, imported or connected
  if (this->GetMRMLScene()->IsBatchProcessing())
  {
    return;
  }

  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::OnMRMLSceneNodeRemoved(vtkMRMLNode* node)
{
  if (!node->IsA("vtkMRMLDisplayableNode") //
      && !node->IsA("vtkMRMLDisplayNode"))
  {
    return;
  }

  this->SetUpdateFromMRMLRequested(true);

  // Escape if the scene a scene is being closed, imported or connected
  if (this->GetMRMLScene()->IsBatchProcessing())
  {
    return;
  }

  //// Node specific processing
  // if (node->IsA("vtkMRMLDisplayableNode"))
  //{
  //   this->RemoveDisplayable(vtkMRMLDisplayableNode::SafeDownCast(node));
  // }

  this->RequestRender();
}

//---------------------------------------------------------------------------
bool vtkMRMLModelDisplayableManager::IsModelDisplayable(vtkMRMLModelNode* modelNode) const
{
  if (!modelNode)
  {
    /// issue 2666: don't manage annotation nodes - don't show lines between the control points
    return false;
  }

  if (modelNode && modelNode->GetMesh())
  {
    return true;
  }

  // Maybe a model node has no mesh but its display nodes have output
  //  (e.g. vtkMRMLGlyphableVolumeSliceDisplayNode).
  bool displayable = false;
  for (int i = 0; i < modelNode->GetNumberOfDisplayNodes(); ++i)
  {
    vtkMRMLModelDisplayNode* modelDisplayNode = vtkMRMLModelDisplayNode::SafeDownCast(modelNode->GetNthDisplayNode(i));
    displayable |= this->IsModelDisplayable(modelDisplayNode);
    if (displayable)
    {
      // Optimization: no need to search any further.
      break;
    }
  }
  return displayable;
}

//---------------------------------------------------------------------------
bool vtkMRMLModelDisplayableManager::IsModelDisplayable(vtkMRMLModelDisplayNode* node) const
{
  vtkMRMLModelDisplayNode* modelDisplayNode = vtkMRMLModelDisplayNode::SafeDownCast(node);
  if (!modelDisplayNode)
  {
    return false;
  }
  if (modelDisplayNode->IsA("vtkMRMLAnnotationDisplayNode"))
  {
    /// issue 2666: don't manage annotation nodes - don't show lines between the control points
    return false;
  }
  return modelDisplayNode->GetOutputMesh() ? true : false;
}

//---------------------------------------------------------------------------
bool vtkMRMLModelDisplayableManager::OnMRMLDisplayableModelNodeModifiedEvent(vtkMRMLModelNode* modelNode)
{
  if (!modelNode)
  {
    vtkErrorMacro("OnMRMLDisplayableModelNodeModifiedEvent: No model node given");
    return false;
  }

  // If the node is already cached with an actor process only this one
  // If it was not visible and is still not visible do nothing
  int numberOfDisplayNodes = modelNode->GetNumberOfDisplayNodes();
  bool updateModel = false;
  bool updateMRML = false;
  bool modelDisplayable = this->IsModelDisplayable(modelNode);
  for (int i = 0; i < numberOfDisplayNodes; i++)
  {
    vtkMRMLModelDisplayNode* dnode = vtkMRMLModelDisplayNode::SafeDownCast(modelNode->GetNthDisplayNode(i));
    if (dnode == nullptr)
    {
      // display node has been removed
      updateMRML = true;
      break;
    }
    bool visible = modelDisplayable && //
                   (dnode->GetVisibility() == 1) && (dnode->GetVisibility3D() == 1) && this->IsModelDisplayable(dnode);
    vtkInternal::ModelDisplayPipeline* pipeline = this->Internal->GetDisplayPipeline(dnode->GetID());

    // If the displayNode is visible and doesn't have actors yet, then request an update
    if (visible && !pipeline)
    {
      updateMRML = true;
      break;
    }

    // If the displayNode visibility has changed or displayNode is visible, then
    // update the model.
    if (!(!visible && this->GetDisplayedModelsVisibility(dnode) == 0))
    {
      updateModel = true;
      break;
    }
  }
  if (updateModel)
  {
    this->UpdateModifiedModel(modelNode);
  }
  if (updateMRML)
  {
    this->SetUpdateFromMRMLRequested(true);
  }
  return updateModel || updateMRML;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::UpdateFromMRML()
{
  if (this->GetInteractor()                       //
      && this->GetInteractor()->GetRenderWindow() //
      && this->GetInteractor()->GetRenderWindow()->CheckInRenderStatus())
  {
    vtkDebugMacro("skipping update during render");
    return;
  }

  // UpdateModelsFromMRML may recursively trigger calling of UpdateModelsFromMRML
  // via node reference updates. IsUpdatingModelsFromMRML flag prevents restarting
  // UpdateModelsFromMRML if it is already in progress.
  if (this->Internal->IsUpdatingModelsFromMRML)
  {
    return;
  }
  this->Internal->IsUpdatingModelsFromMRML = true;
  vtkMRMLScene* scene = this->GetMRMLScene();
  vtkMRMLNode* node = nullptr;
  std::vector<vtkMRMLModelNode*> slices;
  std::vector<vtkMRMLModelNode*> nonSlices;

  if (!scene)
  {
    this->RemoveAllDisplayPipelines();
    return;
  }
  this->PruneMissingNodes();

  std::vector<vtkMRMLNode*> modelNodes;
  scene->GetNodesByClass("vtkMRMLModelNode", modelNodes);
  for (vtkMRMLNode* node : modelNodes)
  {
    vtkMRMLModelNode* model = vtkMRMLModelNode::SafeDownCast(node);
    // render slices last so that transparent objects are rendered in front of them
    if (vtkMRMLSliceLogic::IsSliceModelNode(model))
    {
      slices.push_back(model);
    }
    else
    {
      nonSlices.push_back(model);
    }

    int numberOfDisplayNodes = model->GetNumberOfDisplayNodes();
    for (int displayNodeIndex = 0; displayNodeIndex < numberOfDisplayNodes; ++displayNodeIndex)
    {
      vtkMRMLModelDisplayNode* displayNode = vtkMRMLModelDisplayNode::SafeDownCast(model->GetNthDisplayNode(displayNodeIndex));
      if (!displayNode)
      {
        continue;
      }
      this->AddDisplayPipeline(displayNode->GetID());
    }

    this->UpdateModifiedModel(model);
  }

  // TODO: Slice order?

  this->Internal->IsUpdatingModelsFromMRML = false;
  this->SetUpdateFromMRMLRequested(false);
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::AddDisplayPipeline(const std::string& displayNodeID)
{
  if (this->Internal->GetDisplayPipeline(displayNodeID))
  {
    return;
  }

  vtkMRMLModelDisplayNode* displayNode = vtkMRMLModelDisplayNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(displayNodeID.c_str()));
  if (!displayNode)
  {
    vtkErrorMacro("AddDisplayPipeline: No display node with ID " << displayNodeID.c_str() << " found in the scene");
    return;
  }

  this->Internal->DisplayPipelines[displayNodeID] = vtkInternal::ModelDisplayPipeline();
  vtkInternal::ModelDisplayPipeline* pipeline = this->Internal->GetDisplayPipeline(displayNodeID);
  pipeline->DisplayNode = displayNode;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::RemoveDisplayPipeline(const std::string& id)
{
  auto displayPipelineIter = this->Internal->DisplayPipelines.find(id);
  if (displayPipelineIter == this->Internal->DisplayPipelines.end())
  {
    return;
  }

  vtkInternal::ModelDisplayPipeline* displayPipeline = &displayPipelineIter->second;
  if (!displayPipeline)
  {
    return;
  }

  if (displayPipeline->Actor)
  {
    this->GetRenderer()->RemoveViewProp(displayPipeline->Actor);
  }

  if (displayPipeline->CapActor)
  {
    this->GetRenderer()->RemoveViewProp(displayPipeline->CapActor);
  }

  this->Internal->DisplayPipelines.erase(displayPipelineIter);
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::RemoveAllDisplayPipelines()
{
  std::vector<std::string> displayNodeIDsToRemove;
  for (auto iter = this->Internal->DisplayPipelines.begin(); iter != this->Internal->DisplayPipelines.end(); ++iter)
  {
    displayNodeIDsToRemove.push_back(iter->first);
  }

  for (const std::string& displayNodeID : displayNodeIDsToRemove)
  {
    this->RemoveDisplayPipeline(displayNodeID);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::UpdateModifiedModel(vtkMRMLModelNode* model)
{
  this->UpdateModelPipelines(model);
  this->UpdateModelObservers(model);
  this->SetModelDisplayProperty(model);
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::UpdateModelPipelines(vtkMRMLModelNode* modelNode)
{
  // if no model display nodes found, return
  int ndnodes = modelNode->GetNumberOfDisplayNodes();
  bool hasModelDisplayNode = false;
  for (int i = 0; i < ndnodes; i++)
  {
    if (vtkMRMLModelDisplayNode::SafeDownCast(modelNode->GetNthDisplayNode(i)))
    {
      hasModelDisplayNode = true;
      break;
    }
  }
  if (!hasModelDisplayNode)
  {
    return;
  }

  vtkMRMLDisplayNode* hdnode = vtkMRMLFolderDisplayNode::GetOverridingHierarchyDisplayNode(modelNode);

  for (int i = 0; i < ndnodes; i++)
  {
    vtkMRMLModelDisplayNode* modelDisplayNode = vtkMRMLModelDisplayNode::SafeDownCast(modelNode->GetNthDisplayNode(i));
    if (!modelDisplayNode)
    {
      continue;
    }

    if (!this->IsModelDisplayable(modelDisplayNode))
    {
      this->RemoveDisplayPipeline(modelDisplayNode->GetID());
      continue;
    }

    this->AddDisplayPipeline(modelDisplayNode->GetID());
    vtkInternal::ModelDisplayPipeline* pipeline = this->Internal->GetDisplayPipeline(modelDisplayNode->GetID());

    int clipping = hdnode ? hdnode->GetClipping() : modelDisplayNode->GetClipping();
    int clipState = pipeline->ClipState;

    this->UpdateTransformPipeline(modelDisplayNode, modelNode);
    bool filterUpdateNeeded = this->UpdateClipperPipeline(modelDisplayNode, modelNode);

    // Early exit if clip state unchanged and mapper doesn't need updating
    if (clipState == clipping && pipeline->Actor)
    {
      vtkActor* actor = vtkActor::SafeDownCast(pipeline->Actor);
      vtkMRMLTransformNode* tnode = modelNode->GetParentTransformNode();
      bool mapperUpdateNeeded = true;
      if (actor && actor->GetMapper())
      {
        vtkMapper* mapper = actor->GetMapper();
        vtkMRMLModelNode::MeshTypeHint meshType = modelNode->GetMeshType();
        if ((meshType == vtkMRMLModelNode::UnstructuredGridMeshType && mapper->IsA("vtkDataSetMapper"))
            || (meshType == vtkMRMLModelNode::PolyDataMeshType && mapper->IsA("vtkPolyDataMapper")))
        {
          mapperUpdateNeeded = false;
        }
      }
      if ((!clipping || tnode == nullptr) && !mapperUpdateNeeded && !filterUpdateNeeded)
      {
        continue;
      }
    }

    pipeline->ClipState = clipping;
    this->UpdateMapperPipeline(modelDisplayNode, modelNode);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::UpdateTransformPipeline(vtkMRMLModelDisplayNode* displayNode, vtkMRMLModelNode* modelNode)
{
  if (!displayNode || !modelNode)
  {
    return;
  }

  vtkInternal::ModelDisplayPipeline* pipeline = this->Internal->GetDisplayPipeline(displayNode->GetID());
  if (!pipeline)
  {
    return;
  }

  vtkMRMLTransformNode* tnode = modelNode->GetParentTransformNode();
  if (!tnode || tnode->IsTransformToWorldLinear())
  {
    if (pipeline->TransformFilter)
    {
      pipeline->TransformFilter->SetInputConnection(nullptr);
      pipeline->TransformFilter->SetTransform(nullptr);
      pipeline->TransformFilter = nullptr;
    }
    return;
  }

  if (!pipeline->TransformFilter)
  {
    pipeline->TransformFilter = vtkSmartPointer<vtkTransformFilter>::New();
  }

  vtkAlgorithmOutput* meshConnection = displayNode->GetOutputMeshConnection();
  pipeline->TransformFilter->SetInputConnection(meshConnection);

  vtkSmartPointer<vtkGeneralTransform> worldTransform = vtkSmartPointer<vtkGeneralTransform>::New();
  tnode->GetTransformToWorld(worldTransform);

  if (!vtkMRMLTransformNode::AreTransformsEqual(worldTransform, pipeline->TransformFilter->GetTransform()))
  {
    pipeline->TransformFilter->SetTransform(worldTransform);
  }
}

//---------------------------------------------------------------------------
bool vtkMRMLModelDisplayableManager::UpdateClipperPipeline(vtkMRMLModelDisplayNode* displayNode, vtkMRMLModelNode* modelNode)
{
  if (!displayNode || !modelNode)
  {
    return false;
  }

  vtkInternal::ModelDisplayPipeline* pipeline = this->Internal->GetDisplayPipeline(displayNode->GetID());
  if (!pipeline)
  {
    return false;
  }

  vtkMRMLClipNode* clipNode = displayNode->GetClipNode();
  if (!displayNode->GetClipping() || !clipNode)
  {
    bool changed = pipeline->Clipper != nullptr;
    pipeline->Clipper = nullptr;
    pipeline->Capper = nullptr;
    return changed;
  }

  // Check if all clipping is off
  bool allClippingOff = true;
  for (int i = 0; i < clipNode->GetNumberOfClippingNodes(); ++i)
  {
    if (clipNode->GetNthClippingNodeState(i) != vtkMRMLClipNode::ClipOff)
    {
      allClippingOff = false;
      break;
    }
  }
  if (allClippingOff)
  {
    bool changed = pipeline->Clipper != nullptr;
    pipeline->Clipper = nullptr;
    pipeline->Capper = nullptr;
    return changed;
  }

  vtkImplicitFunction* implicitFunction = clipNode->GetImplicitFunctionWorld();
  if (!implicitFunction)
  {
    bool changed = pipeline->Clipper != nullptr;
    pipeline->Clipper = nullptr;
    pipeline->Capper = nullptr;
    return changed;
  }

  vtkSmartPointer<vtkImplicitBoolean> implicitBoolean = vtkSmartPointer<vtkImplicitBoolean>::New();
  implicitBoolean->AddFunction(implicitFunction);

  // Apply linear transform to implicit function.
  // Non-linear transforms are handled by TransformFilter on the mesh itself.
  vtkMRMLTransformNode* tnode = modelNode->GetParentTransformNode();
  if (tnode && tnode->IsTransformToWorldLinear())
  {
    vtkSmartPointer<vtkGeneralTransform> worldTransform = vtkSmartPointer<vtkGeneralTransform>::New();
    tnode->GetTransformToWorld(worldTransform);
    implicitBoolean->SetTransform(worldTransform);
  }

  vtkAlgorithmOutput* meshConnection = pipeline->TransformFilter ? pipeline->TransformFilter->GetOutputPort() : displayNode->GetOutputMeshConnection();

  vtkSmartPointer<vtkAlgorithm> oldClipper = pipeline->Clipper;
  pipeline->Clipper = this->GetClipper(displayNode, modelNode->GetMeshType(), implicitBoolean, clipNode->GetClippingMethod());
  pipeline->Clipper->SetInputConnection(meshConnection);

  if (!pipeline->Capper)
  {
    pipeline->Capper = vtkSmartPointer<vtkCapPolyData>::New();
  }
  pipeline->Capper->SetClipFunction(implicitBoolean);
  pipeline->Capper->SetInputConnection(meshConnection);

  return oldClipper != pipeline->Clipper;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::UpdateMapperPipeline(vtkMRMLModelDisplayNode* displayNode, vtkMRMLModelNode* modelNode)
{
  if (!displayNode || !modelNode)
  {
    return;
  }

  vtkInternal::ModelDisplayPipeline* pipeline = this->Internal->GetDisplayPipeline(displayNode->GetID());
  if (!pipeline)
  {
    return;
  }

  vtkMRMLModelNode::MeshTypeHint meshType = modelNode->GetMeshType();

  vtkAlgorithmOutput* meshConnection = pipeline->TransformFilter ? pipeline->TransformFilter->GetOutputPort() : displayNode->GetOutputMeshConnection();

  // Create actor if needed
  if (!pipeline->Actor)
  {
    pipeline->Actor = vtkSmartPointer<vtkActor>::New();
    this->GetRenderer()->AddViewProp(pipeline->Actor);
  }

  // Create cap actor if needed
  if (!pipeline->CapActor)
  {
    pipeline->CapActor = vtkSmartPointer<vtkActor>::New();
    this->GetRenderer()->AddViewProp(pipeline->CapActor);
  }

  vtkActor* actor = vtkActor::SafeDownCast(pipeline->Actor);
  vtkActor* capActor = vtkActor::SafeDownCast(pipeline->CapActor);

  // Build main mapper
  vtkSmartPointer<vtkMapper> mapper;
  if (meshType == vtkMRMLModelNode::UnstructuredGridMeshType)
  {
    mapper = vtkSmartPointer<vtkDataSetMapper>::New();
  }
  else
  {
    mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  }

  if (pipeline->Clipper)
  {
    mapper->SetInputConnection(pipeline->Clipper->GetOutputPort());
  }
  else
  {
    mapper->SetInputConnection(meshConnection);
  }
  actor->SetMapper(mapper);

  // Build cap mapper
  if (pipeline->Capper)
  {
    vtkSmartPointer<vtkMapper> capMapper;
    if (meshType == vtkMRMLModelNode::UnstructuredGridMeshType)
    {
      capMapper = vtkSmartPointer<vtkDataSetMapper>::New();
    }
    else
    {
      capMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    }
    capMapper->SetInputConnection(pipeline->Capper->GetOutputPort());
    capActor->SetMapper(capMapper);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::UpdateModelObservers(vtkMRMLModelNode* model)
{
  vtkEventBroker* broker = vtkEventBroker::GetInstance();
  for (int event : this->Internal->ModelNodeEvents)
  {
    if (!broker->GetObservationExist(model, event, this, this->GetMRMLNodesCallbackCommand()))
    {
      broker->AddObservation(model, event, this, this->GetMRMLNodesCallbackCommand());
    }
  }
  this->Internal->ObservedModelNodes.insert(model);
}

//---------------------------------------------------------------------------
int vtkMRMLModelDisplayableManager::GetDisplayedModelsVisibility(vtkMRMLModelDisplayNode* displayNode)
{
  if (!displayNode)
  {
    vtkErrorMacro("GetDisplayedModelsVisibility: No display node given");
    return 0;
  }

  vtkInternal::ModelDisplayPipeline* pipeline = this->Internal->GetDisplayPipeline(displayNode->GetID());
  vtkProp3D* actor = pipeline ? pipeline->Actor : nullptr;
  return actor ? actor->GetVisibility() : false;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::RemoveMRMLObservers()
{
  this->RemoveModelObservers();
  this->Superclass::RemoveMRMLObservers();
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::RemoveModelObservers()
{
  std::set<vtkWeakPointer<vtkMRMLModelNode>> observedModelNodesCopy = this->Internal->ObservedModelNodes;
  for (vtkMRMLModelNode* modelNode : observedModelNodesCopy)
  {
    if (modelNode)
    {
      this->RemoveDisplayableNodeObservers(modelNode);
    }
  }

  // Observers should already be removed in the loop above, but clear the set in case there were any nullptr
  this->Internal->ObservedModelNodes.clear();
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::RemoveDisplayableNodeObservers(vtkMRMLModelNode* model)
{
  if (model == nullptr)
  {
    return;
  }

  vtkEventBroker* broker = vtkEventBroker::GetInstance();
  vtkEventBroker::ObservationVector observations;
  for (int event : this->Internal->ModelNodeEvents)
  {
    observations = broker->GetObservations(model, event, this, this->GetMRMLNodesCallbackCommand());
    broker->RemoveObservations(observations);
  }
  this->Internal->ObservedModelNodes.erase(model);
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::PruneMissingNodes()
{
  if (!this->GetMRMLScene())
  {
    return;
  }

  std::vector<std::string> displayNodeIDsToRemove;
  for (auto iter = this->Internal->DisplayPipelines.begin(); iter != this->Internal->DisplayPipelines.end(); iter++)
  {
    vtkMRMLNode* modelDisplayNode = this->GetMRMLScene()->GetNodeByID(iter->first);
    if (modelDisplayNode == nullptr || !iter->second.DisplayNode)
    {
      displayNodeIDsToRemove.push_back(iter->first);
    }
  }
  for (auto displayNodeID : displayNodeIDsToRemove)
  {
    this->RemoveDisplayPipeline(displayNodeID);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::SetModelDisplayProperty(vtkMRMLModelNode* model)
{
  // Get transformation applied on model
  vtkMRMLTransformNode* transformNode = model->GetParentTransformNode();
  vtkNew<vtkMatrix4x4> matrixTransformToWorld;
  if (transformNode != nullptr && transformNode->IsTransformToWorldLinear())
  {
    transformNode->GetMatrixTransformToWorld(matrixTransformToWorld.GetPointer());
  }

  // Get display node from hierarchy that applies display properties on branch
  vtkMRMLDisplayNode* overrideHierarchyDisplayNode = vtkMRMLFolderDisplayNode::GetOverridingHierarchyDisplayNode(model);

  // Set display properties to props for all display nodes
  int numberOfDisplayNodes = model->GetNumberOfDisplayNodes();
  for (int i = 0; i < numberOfDisplayNodes; i++)
  {
    vtkMRMLDisplayNode* displayNode = model->GetNthDisplayNode(i);
    vtkMRMLModelDisplayNode* modelDisplayNode = vtkMRMLModelDisplayNode::SafeDownCast(displayNode);
    if (!modelDisplayNode)
    {
      continue;
    }
    vtkProp3D* prop = this->GetActorByID(modelDisplayNode->GetID());
    if (prop == nullptr)
    {
      continue;
    }

    // Use hierarchy display node if any, and if overriding is allowed for the current display node.
    // If override is explicitly disabled, then do not apply hierarchy visibility or opacity either.
    bool hierarchyVisibility = true;
    double hierarchyOpacity = 1.0;
    if (displayNode->GetFolderDisplayOverrideAllowed())
    {
      if (overrideHierarchyDisplayNode)
      {
        displayNode = overrideHierarchyDisplayNode;
      }

      // Get visibility and opacity defined by the hierarchy.
      // These two properties are influenced by the hierarchy regardless the fact whether there is override
      // or not. Visibility of items defined by hierarchy is off if any of the ancestors is explicitly hidden,
      // and the opacity is the product of the ancestors' opacities.
      // However, this does not apply on display nodes that do not allow overrides (FolderDisplayOverrideAllowed)
      hierarchyVisibility = vtkMRMLFolderDisplayNode::GetHierarchyVisibility(model);
      hierarchyOpacity = vtkMRMLFolderDisplayNode::GetHierarchyOpacity(model);
    }

    vtkInternal::ModelDisplayPipeline* displayPipeline = this->Internal->GetDisplayPipeline(modelDisplayNode->GetID());

    vtkActor* actor = vtkActor::SafeDownCast(prop);
    vtkProp3D* capProp = displayPipeline ? displayPipeline->CapActor : nullptr;
    vtkActor* capActor = vtkActor::SafeDownCast(capProp);
    if (capProp)
    {
      capProp->SetUserMatrix(matrixTransformToWorld);
    }

    vtkImageActor* imageActor = vtkImageActor::SafeDownCast(prop);
    prop->SetUserMatrix(matrixTransformToWorld);

    // If there is an overriding hierarchy display node, then consider its visibility as well
    // as the model's. It is important to consider the model's visibility, because the user will
    // still want to show/hide children regardless of application of display properties from the
    // hierarchy.
    bool visible = hierarchyVisibility                                                         //
                   && modelDisplayNode->GetVisibility() && modelDisplayNode->GetVisibility3D() //
                   && modelDisplayNode->IsDisplayableInView(this->GetMRMLViewNode()->GetID());
    prop->SetVisibility(visible);
    bool capVisible = visible                                       //
                      && modelDisplayNode->GetClipping()            //
                      && modelDisplayNode->GetClipNode() != nullptr //
                      && (modelDisplayNode->GetClippingCapSurface() || modelDisplayNode->GetClippingOutline());
    if (capActor)
    {
      capActor->SetVisibility(capVisible);
    }
    double opacity = hierarchyOpacity * displayNode->GetOpacity();

    if (visible && actor)
    {
      this->UpdateMapperProperties(vtkMRMLModelNode::SafeDownCast(model), displayNode, actor->GetMapper());
      this->UpdateActorProperties(vtkMRMLModelNode::SafeDownCast(model), modelDisplayNode, displayNode, actor, opacity);
    }

    if (capVisible && capActor)
    {
      this->UpdateMapperProperties(vtkMRMLModelNode::SafeDownCast(model), displayNode, capActor->GetMapper());
      this->UpdateCapActorProperties(vtkMRMLModelNode::SafeDownCast(model), modelDisplayNode, displayNode, capActor, opacity);
    }

    if (imageActor)
    {
      imageActor->GetMapper()->SetInputConnection(displayNode->GetTextureImageDataConnection());
      imageActor->SetDisplayExtent(-1, 0, 0, 0, 0, 0);
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::UpdateMapperProperties(vtkMRMLModelNode* modelNode, vtkMRMLDisplayNode* displayNode, vtkMapper* mapper)
{
  if (!mapper)
  {
    return;
  }

  mapper->SetScalarVisibility(displayNode->GetScalarVisibility());
  // if the scalars are visible, set active scalars, the lookup table
  // and the scalar range
  if (displayNode->GetScalarVisibility())
  {
    // Check if using point data or cell data
    if (this->IsCellScalarsActive(displayNode, modelNode))
    {
      mapper->SetScalarModeToUseCellData();
    }
    else
    {
      mapper->SetScalarModeToUsePointData();
    }

    if (displayNode->GetScalarRangeFlag() == vtkMRMLDisplayNode::UseDirectMapping)
    {
      mapper->UseLookupTableScalarRangeOn(); // avoid warning about bad table range
      mapper->SetColorModeToDirectScalars();
      mapper->SetLookupTable(nullptr);
    }
    else
    {
      mapper->UseLookupTableScalarRangeOff();
      mapper->SetColorModeToMapScalars();

      // The renderer uses the lookup table scalar range to
      // render colors. By default, UseLookupTableScalarRange
      // is set to false and SetScalarRange can be used on the
      // mapper to map scalars into the lookup table. When set
      // to true, SetScalarRange has no effect and it is necessary
      // to force the scalarRange on the lookup table manually.
      // Whichever way is used, the look up table range needs
      // to be changed to render the correct scalar values, thus
      // one lookup table can not be shared by multiple mappers
      // if any of those mappers needs to map using its scalar
      // values range. It is therefore necessary to make a copy
      // of the colorNode vtkLookupTable in order not to impact
      // that lookup table original range.
      vtkSmartPointer<vtkLookupTable> dNodeLUT =
        vtkSmartPointer<vtkLookupTable>::Take(displayNode->GetColorNode() ? displayNode->GetColorNode()->CreateLookupTableCopy() : nullptr);
      mapper->SetLookupTable(dNodeLUT);
    }

    // Set scalar range
    mapper->SetScalarRange(displayNode->GetScalarRange());
  }
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::UpdateActorProperties(vtkMRMLModelNode* modelNode,
                                                           vtkMRMLModelDisplayNode* modelDisplayNode,
                                                           vtkMRMLDisplayNode* displayNode,
                                                           vtkActor* actor,
                                                           double opacity)
{
  if (!modelNode || !modelDisplayNode || !actor)
  {
    return;
  }

  if (!displayNode)
  {
    displayNode = modelDisplayNode;
  }

  vtkProperty* actorProperties = actor->GetProperty();
  actorProperties->SetRepresentation(displayNode->GetRepresentation());
  actorProperties->SetPointSize(displayNode->GetPointSize());
  actorProperties->SetLineWidth(displayNode->GetLineWidth());
  actorProperties->SetLighting(displayNode->GetLighting());
  actorProperties->SetInterpolation(displayNode->GetInterpolation());
  actorProperties->SetShading(displayNode->GetShading());
  actorProperties->SetFrontfaceCulling(displayNode->GetFrontfaceCulling());
  actorProperties->SetBackfaceCulling(displayNode->GetBackfaceCulling());

  actor->SetPickable(modelNode->GetSelectable());
  if (displayNode->GetSelected())
  {
    actorProperties->SetColor(displayNode->GetSelectedColor());
    actorProperties->SetAmbient(displayNode->GetSelectedAmbient());
    actorProperties->SetSpecular(displayNode->GetSelectedSpecular());
  }
  else
  {
    actorProperties->SetColor(displayNode->GetColor());
    actorProperties->SetAmbient(displayNode->GetAmbient());
    actorProperties->SetSpecular(displayNode->GetSpecular());
  }
  // Opacity will be the product of the opacities of the model and the overriding
  // hierarchy, in order to keep the relative opacities the same.
  actorProperties->SetOpacity(opacity);
  actorProperties->SetDiffuse(displayNode->GetDiffuse());
  actorProperties->SetSpecularPower(displayNode->GetPower());
  actorProperties->SetMetallic(displayNode->GetMetallic());
  actorProperties->SetRoughness(displayNode->GetRoughness());
  actorProperties->SetEdgeVisibility(displayNode->GetEdgeVisibility());
  actorProperties->SetEdgeColor(displayNode->GetEdgeColor());

  if (displayNode->GetTextureImageDataConnection() != nullptr)
  {
    if (actor->GetTexture() == nullptr)
    {
      vtkNew<vtkTexture> texture;
      actor->SetTexture(texture);
    }
    actor->GetTexture()->SetInputConnection(displayNode->GetTextureImageDataConnection());
    actor->GetTexture()->SetInterpolate(displayNode->GetInterpolateTexture());
    actorProperties->SetColor(1., 1., 1.);

    // Force actors to be treated as opaque. Otherwise, transparent
    // elements in the texture cause the actor to be treated as
    // translucent, i.e. rendered without writing to the depth buffer.
    // See https://github.com/Slicer/Slicer/issues/4253.
    actor->SetForceOpaque(actorProperties->GetOpacity() >= 1.0);
  }
  else
  {
    actor->SetTexture(nullptr);
    actor->ForceOpaqueOff();
  }

  // Set backface properties
  vtkProperty* actorBackfaceProperties = actor->GetBackfaceProperty();
  if (!actorBackfaceProperties)
  {
    vtkNew<vtkProperty> newActorBackfaceProperties;
    actor->SetBackfaceProperty(newActorBackfaceProperties);
    actorBackfaceProperties = newActorBackfaceProperties;
  }
  actorBackfaceProperties->DeepCopy(actorProperties);

  double offsetHsv[3];
  modelDisplayNode->GetBackfaceColorHSVOffset(offsetHsv);

  double colorHsv[3];
  vtkMath::RGBToHSV(actorProperties->GetColor(), colorHsv);
  double colorRgb[3];
  colorHsv[0] += offsetHsv[0];
  // wrap around hue value
  if (colorHsv[0] < 0.0)
  {
    colorHsv[0] += 1.0;
  }
  else if (colorHsv[0] > 1.0)
  {
    colorHsv[0] -= 1.0;
  }
  colorHsv[1] = vtkMath::ClampValue<double>(colorHsv[1] + offsetHsv[1], 0, 1);
  colorHsv[2] = vtkMath::ClampValue<double>(colorHsv[2] + offsetHsv[2], 0, 1);
  vtkMath::HSVToRGB(colorHsv, colorRgb);
  actorBackfaceProperties->SetColor(colorRgb);
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::UpdateCapActorProperties(vtkMRMLModelNode* modelNode,
                                                              vtkMRMLModelDisplayNode* modelDisplayNode,
                                                              vtkMRMLDisplayNode* displayNode,
                                                              vtkActor* capActor,
                                                              double opacity)
{
  if (!displayNode)
  {
    displayNode = modelDisplayNode;
  }

  this->UpdateActorProperties(modelNode, modelDisplayNode, displayNode, capActor, opacity);

  vtkSmartPointer<vtkProperty> capActorProperties = capActor->GetProperty();
  if (!capActorProperties)
  {
    capActorProperties = vtkSmartPointer<vtkProperty>::New();
    capActor->SetProperty(capActorProperties);
  }
  capActorProperties->SetLineWidth(modelDisplayNode->GetLineWidth());

  vtkMapper* capMapper = capActor->GetMapper();
  if (!capMapper)
  {
    return;
  }

  double offsetHsv[3];
  modelDisplayNode->GetClippingCapColorHSVOffset(offsetHsv);

  double colorHsv[3];
  vtkMath::RGBToHSV(capActorProperties->GetColor(), colorHsv);
  colorHsv[0] += offsetHsv[0];
  // wrap around hue value
  if (colorHsv[0] < 0.0)
  {
    colorHsv[0] += 1.0;
  }
  else if (colorHsv[0] > 1.0)
  {
    colorHsv[0] -= 1.0;
  }
  colorHsv[1] = vtkMath::ClampValue<double>(colorHsv[1] + offsetHsv[1], 0.0, 1.0);
  colorHsv[2] = vtkMath::ClampValue<double>(colorHsv[2] + offsetHsv[2], 0.0, 1.0);

  double colorRgb[3];
  vtkMath::HSVToRGB(colorHsv, colorRgb);
  capActorProperties->SetColor(colorRgb);

  bool capSurface = modelDisplayNode ? modelDisplayNode->GetClippingCapSurface() : false;
  bool clipOutline = modelDisplayNode ? modelDisplayNode->GetClippingOutline() : false;

  double capOpacity = capSurface ? opacity * modelDisplayNode->GetClippingCapOpacity() : 0.0;
  double outlineOpacity = clipOutline ? opacity : 0.0;

  double edgeColor[4] = { 1.0, 0.0, 0.0, 1.0 };
  modelDisplayNode->GetEdgeColor(edgeColor);
  edgeColor[3] = outlineOpacity;

  // Create a lookup table to map cell data to colors.
  vtkNew<vtkLookupTable> lut;
  lut->SetTableRange(VTK_LINE, VTK_POLY_LINE);
  lut->SetNumberOfColors(1);
  lut->Build();
  lut->SetTableValue(0, edgeColor);
  lut->UseBelowRangeColorOn();
  lut->UseAboveRangeColorOn();
  lut->SetBelowRangeColor(colorRgb[0], colorRgb[1], colorRgb[2], capOpacity);
  lut->SetAboveRangeColor(colorRgb[0], colorRgb[1], colorRgb[2], capOpacity);

  capMapper->SetLookupTable(lut);
  capMapper->UseLookupTableScalarRangeOn();
  capMapper->SetScalarModeToUseCellData();
  capMapper->SetColorModeToMapScalars();
  capMapper->SetScalarVisibility(true);
}

//---------------------------------------------------------------------------
const char* vtkMRMLModelDisplayableManager::GetActiveScalarName(vtkMRMLModelDisplayNode* displayNode, vtkMRMLModelNode* modelNode)
{
  const char* activeScalarName = nullptr;
  if (displayNode)
  {
    vtkMRMLModelDisplayNode* modelDisplayNode = vtkMRMLModelDisplayNode::SafeDownCast(displayNode);
    if (modelDisplayNode && modelDisplayNode->GetOutputMesh())
    {
      modelDisplayNode->GetOutputMeshConnection()->GetProducer()->Update();
    }
    activeScalarName = displayNode->GetActiveScalarName();
  }
  if (activeScalarName)
  {
    return activeScalarName;
  }
  if (modelNode)
  {
    if (modelNode->GetMesh())
    {
      vtkAlgorithmOutput* meshConnection = modelNode->GetMeshConnection();
      if (meshConnection != nullptr)
      {
        meshConnection->GetProducer()->Update();
      }
    }
    activeScalarName = modelNode->GetActiveCellScalarName(vtkDataSetAttributes::SCALARS);
    if (activeScalarName)
    {
      return activeScalarName;
    }
    activeScalarName = modelNode->GetActivePointScalarName(vtkDataSetAttributes::SCALARS);
    if (activeScalarName)
    {
      return activeScalarName;
    }
  }
  return nullptr;
}

//---------------------------------------------------------------------------
bool vtkMRMLModelDisplayableManager::IsCellScalarsActive(vtkMRMLDisplayNode* displayNode, vtkMRMLModelNode* modelNode)
{
  if (displayNode && displayNode->GetActiveScalarName())
  {
    return (displayNode->GetActiveAttributeLocation() == vtkAssignAttribute::CELL_DATA);
  }
  if (modelNode && //
      modelNode->GetActiveCellScalarName(vtkDataSetAttributes::SCALARS))
  {
    return true;
  }
  return false;
}

//---------------------------------------------------------------------------
// Description:
// return the current actor corresponding to a give MRML ID
vtkProp3D* vtkMRMLModelDisplayableManager::GetActorByID(const char* id)
{
  if (!id)
  {
    return nullptr;
  }

  auto pipelineIt = this->Internal->DisplayPipelines.find(id);
  if (pipelineIt != this->Internal->DisplayPipelines.end())
  {
    return pipelineIt->second.Actor;
  }

  return nullptr;
}

//---------------------------------------------------------------------------
// Description:
// return the ID for the given actor
const char* vtkMRMLModelDisplayableManager::GetIDByActor(vtkProp3D* actor)
{
  if (!actor)
  {
    return nullptr;
  }

  for (auto iter = this->Internal->DisplayPipelines.begin(); iter != this->Internal->DisplayPipelines.end(); iter++)
  {
    if (iter->second.Actor == actor)
    {
      return (iter->first.c_str());
    }
  }
  return nullptr;
}

//---------------------------------------------------------------------------
vtkWorldPointPicker* vtkMRMLModelDisplayableManager::GetWorldPointPicker()
{
  vtkDebugMacro(<< "returning Internal->WorldPointPicker address " << this->Internal->WorldPointPicker.GetPointer());
  return this->Internal->WorldPointPicker;
}

//---------------------------------------------------------------------------
vtkPropPicker* vtkMRMLModelDisplayableManager::GetPropPicker()
{
  vtkDebugMacro(<< "returning Internal->PropPicker address " << this->Internal->PropPicker.GetPointer());
  return this->Internal->PropPicker;
}

//---------------------------------------------------------------------------
vtkCellPicker* vtkMRMLModelDisplayableManager::GetCellPicker()
{
  vtkDebugMacro(<< "returning Internal->CellPicker address " << this->Internal->CellPicker.GetPointer());
  return this->Internal->CellPicker;
}

//---------------------------------------------------------------------------
vtkPointPicker* vtkMRMLModelDisplayableManager::GetPointPicker()
{
  vtkDebugMacro(<< "returning Internal->PointPicker address " << this->Internal->PointPicker.GetPointer());
  return this->Internal->PointPicker;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::SetPickTolerance(double tolerance)
{
  this->Internal->CellPicker->SetTolerance(tolerance);
}

//---------------------------------------------------------------------------
double vtkMRMLModelDisplayableManager::GetPickTolerance()
{
  return this->Internal->CellPicker->GetTolerance();
}

//---------------------------------------------------------------------------
int vtkMRMLModelDisplayableManager::Pick(int x, int y)
{
  double RASPoint[3] = { 0.0, 0.0, 0.0 };
  double pickPoint[3] = { 0.0, 0.0, 0.0 };

  // Reset the pick vars
  this->Internal->ResetPick();

  vtkRenderer* ren = this->GetRenderer();
  if (!ren)
  {
    vtkErrorMacro("Pick: unable to get renderer\n");
    return 0;
  }
  // get the current renderer's size
  int* renSize = ren->GetSize();
  // resize the interactor?

  // pass the event's display point to the world point picker
  double displayPoint[3];
  displayPoint[0] = x;
  displayPoint[1] = renSize[1] - y;
  displayPoint[2] = 0.0;

  if (this->Internal->CellPicker->Pick(displayPoint[0], displayPoint[1], displayPoint[2], ren))
  {
    this->Internal->CellPicker->GetPickPosition(pickPoint);
    this->SetPickedCellID(this->Internal->CellPicker->GetCellId());

    // look for either picked mesh or volume
    // and set picked display node accordingly
    vtkPointSet* mesh = vtkPointSet::SafeDownCast(this->Internal->CellPicker->GetDataSet());
    if (mesh)
    {
      // get the pointer to the mesh that the cell was in
      // and then find the model this mesh belongs to
      this->Internal->FindPickedDisplayNodeFromMesh(mesh, pickPoint);
    }
    vtkImageData* imageData = vtkImageData::SafeDownCast(this->Internal->CellPicker->GetDataSet());
    if (imageData)
    {
      // get the pointer to the picked imageData
      // and then find the volume this imageData belongs to
      this->Internal->FindDisplayNodeFromImageData(this->GetMRMLScene(), imageData);
    }
  }
  else
  {
    // there may not have been an actor at the picked point, but the Pick should be translated to a valid position
    // TBD: warn the user that they're picking in empty space?
    this->Internal->CellPicker->GetPickPosition(pickPoint);
  }

  // translate world to RAS
  for (int p = 0; p < 3; p++)
  {
    RASPoint[p] = pickPoint[p];
  }

  // now set up the class vars
  this->SetPickedRAS(RASPoint);

  return 1;
}

//---------------------------------------------------------------------------
int vtkMRMLModelDisplayableManager::Pick3D(double ras[3])
{
  // Reset the pick vars
  this->Internal->ResetPick();

  vtkRenderer* ren = this->GetRenderer();
  if (!ren)
  {
    vtkErrorMacro("Pick3D: Unable to get renderer");
    return 0;
  }

  if (this->Internal->CellPicker->Pick3DPoint(ras, ren))
  {
    this->SetPickedCellID(this->Internal->CellPicker->GetCellId());

    // Find first picked model from picker
    // Note: Getting the mesh using GetDataSet is not a good solution as the dataset is the first
    //   one that is picked and it may be of different type (volume, segmentation, etc.)
    this->Internal->FindFirstPickedDisplayNodeFromPickerProp3Ds();
    // Find picked point in mesh
    vtkMRMLModelDisplayNode* displayNode = vtkMRMLModelDisplayNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(this->Internal->PickedDisplayNodeID.c_str()));
    if (displayNode)
    {
      this->Internal->FindPickedPointOnMeshAndCell(displayNode->GetOutputMesh(), ras);
    }

    this->SetPickedRAS(ras);
  }

  return 1;
}

//---------------------------------------------------------------------------
const char* vtkMRMLModelDisplayableManager::GetPickedNodeID()
{
  vtkDebugMacro(<< "returning this->Internal->PickedDisplayNodeID of " << (this->Internal->PickedDisplayNodeID.empty() ? "(empty)" : this->Internal->PickedDisplayNodeID));
  return this->Internal->PickedDisplayNodeID.c_str();
}

//---------------------------------------------------------------------------
double* vtkMRMLModelDisplayableManager::GetPickedRAS()
{
  vtkDebugMacro(<< "returning Internal->PickedRAS pointer " << this->Internal->PickedRAS);
  return this->Internal->PickedRAS;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::SetPickedRAS(double* newPickedRAS)
{
  int i;
  for (i = 0; i < 3; i++)
  {
    if (newPickedRAS[i] != this->Internal->PickedRAS[i])
    {
      break;
    }
  }
  if (i < 3)
  {
    for (i = 0; i < 3; i++)
    {
      this->Internal->PickedRAS[i] = newPickedRAS[i];
    }
    this->Modified();
  }
}

//---------------------------------------------------------------------------
vtkIdType vtkMRMLModelDisplayableManager::GetPickedCellID()
{
  vtkDebugMacro(<< "returning this->Internal->PickedCellID of " << this->Internal->PickedCellID);
  return this->Internal->PickedCellID;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::SetPickedCellID(vtkIdType newCellID)
{
  vtkDebugMacro(<< "setting PickedCellID to " << newCellID);
  if (this->Internal->PickedCellID != newCellID)
  {
    this->Internal->PickedCellID = newCellID;
    this->Modified();
  }
}

//---------------------------------------------------------------------------
vtkIdType vtkMRMLModelDisplayableManager::GetPickedPointID()
{
  vtkDebugMacro(<< "returning this->Internal->PickedPointID of " << this->Internal->PickedPointID);
  return this->Internal->PickedPointID;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::SetPickedPointID(vtkIdType newPointID)
{
  vtkDebugMacro(<< "setting PickedPointID to " << newPointID);
  if (this->Internal->PickedPointID != newPointID)
  {
    this->Internal->PickedPointID = newPointID;
    this->Modified();
  }
}

//---------------------------------------------------------------------------
vtkAlgorithm* vtkMRMLModelDisplayableManager::GetClipper(vtkMRMLModelDisplayNode* dnode, vtkMRMLModelNode::MeshTypeHint type, vtkImplicitFunction* clipFunction, int clippingMethod)
{
  if (!dnode || !clipFunction)
  {
    return nullptr;
  }

  vtkInternal::ModelDisplayPipeline* displayPipeline = this->Internal->GetDisplayPipeline(dnode->GetID());
  if (!displayPipeline)
  {
    return nullptr;
  }

  vtkSmartPointer<vtkAlgorithm> clipper = displayPipeline->Clipper;
  if (type == vtkMRMLModelNode::UnstructuredGridMeshType)
  {
    if (clippingMethod == vtkMRMLClipNode::Straight)
    {
      vtkSmartPointer<vtkClipDataSet> clipDataSet = vtkClipDataSet::SafeDownCast(clipper);
      if (!clipDataSet)
      {
        clipDataSet = vtkSmartPointer<vtkClipDataSet>::New();
        clipper = clipDataSet;
      }
      clipDataSet->SetClipFunction(clipFunction);
    }
    else
    {
      vtkSmartPointer<vtkExtractGeometry> extractGeometry = vtkExtractGeometry::SafeDownCast(clipper);
      if (!extractGeometry)
      {
        extractGeometry = vtkSmartPointer<vtkExtractGeometry>::New();
        clipper = extractGeometry;
      }
      extractGeometry->SetImplicitFunction(clipFunction);
      extractGeometry->ExtractInsideOff();
      if (clippingMethod == vtkMRMLClipNode::WholeCellsWithBoundary)
      {
        extractGeometry->ExtractBoundaryCellsOn();
      }
    }
  }
  else
  {
    if (clippingMethod == vtkMRMLClipNode::Straight)
    {
      vtkSmartPointer<vtkClipPolyData> clipPolyData = vtkClipPolyData::SafeDownCast(clipper);
      if (!clipPolyData)
      {
        clipPolyData = vtkSmartPointer<vtkClipPolyData>::New();
        clipper = clipPolyData;
      }
      clipPolyData->SetValue(0.0);
      clipPolyData->SetClipFunction(clipFunction);
    }
    else
    {
      vtkSmartPointer<vtkExtractPolyDataGeometry> extractPolyDataGeometry = vtkExtractPolyDataGeometry::SafeDownCast(clipper);
      if (!extractPolyDataGeometry)
      {
        extractPolyDataGeometry = vtkSmartPointer<vtkExtractPolyDataGeometry>::New();
        clipper = extractPolyDataGeometry;
      }
      extractPolyDataGeometry->SetImplicitFunction(clipFunction);
      extractPolyDataGeometry->ExtractInsideOff();
      if (clippingMethod == vtkMRMLClipNode::WholeCellsWithBoundary)
      {
        extractPolyDataGeometry->ExtractBoundaryCellsOn();
      }
    }
  }

  displayPipeline->Clipper = clipper;
  return clipper;
}

//---------------------------------------------------------------------------
void vtkMRMLModelDisplayableManager::OnInteractorStyleEvent(int eventid)
{
  bool keyPressed = false;
  char* keySym = this->GetInteractor()->GetKeySym();
  if (keySym && strcmp(keySym, "i") == 0)
  {
    keyPressed = true;
  }

  if (eventid == vtkCommand::LeftButtonPressEvent && keyPressed)
  {
    double x = this->GetInteractor()->GetEventPosition()[0];
    double y = this->GetInteractor()->GetEventPosition()[1];

    double windowWidth = this->GetInteractor()->GetRenderWindow()->GetSize()[0];
    double windowHeight = this->GetInteractor()->GetRenderWindow()->GetSize()[1];

    if (x < windowWidth && y < windowHeight)
    {
      // it's a 3D displayable manager and the click could have been on a node
      double yNew = windowHeight - y - 1;
      vtkMRMLDisplayNode* displayNode = nullptr;

      if (this->Pick(x, yNew) //
          && strcmp(this->GetPickedNodeID(), "") != 0)
      {
        // find the node id, the picked node name is probably the display node
        const char* pickedNodeID = this->GetPickedNodeID();

        vtkMRMLNode* mrmlNode = this->GetMRMLScene()->GetNodeByID(pickedNodeID);
        if (mrmlNode)
        {
          displayNode = vtkMRMLDisplayNode::SafeDownCast(mrmlNode);
        }
        else
        {
          vtkDebugMacro("couldn't find a mrml node with ID " << pickedNodeID);
        }
      }

      if (displayNode)
      {
        displayNode->SetColor(1.0, 0, 0);
        this->GetInteractionNode()->SetCurrentInteractionMode(vtkMRMLInteractionNode::ViewTransform);
      }
    }
  }
  if (keyPressed)
  {
    this->GetInteractor()->SetKeySym(nullptr);
  }

  this->PassThroughInteractorStyleEvent(eventid);

  return;
}
