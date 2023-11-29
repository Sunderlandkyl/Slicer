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

// VTK includes
#include <vtkGeneralTransform.h>
#include <vtkMatrix4x4.h>

// Transform MRMLDM includes
#include "vtkMRMLTransformHandleWidgetRepresentation.h"

#include "vtkSlicerTransformLogic.h"

// MRML includes
#include <vtkMRMLFolderDisplayNode.h>
#include <vtkMRMLInteractionEventData.h>
#include <vtkMRMLTransformNode.h>
#include <vtkMRMLViewNode.h>

// vtkAddon includes
#include <vtkAddonMathUtilities.h>

//---------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLTransformHandleWidgetRepresentation);

//----------------------------------------------------------------------
vtkMRMLTransformHandleWidgetRepresentation::vtkMRMLTransformHandleWidgetRepresentation()
{
}

//----------------------------------------------------------------------
vtkMRMLTransformHandleWidgetRepresentation::~vtkMRMLTransformHandleWidgetRepresentation() = default;

//-----------------------------------------------------------------------------
void vtkMRMLTransformHandleWidgetRepresentation::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------
vtkMRMLTransformDisplayNode* vtkMRMLTransformHandleWidgetRepresentation::GetDisplayNode()
{
  return this->DisplayNode;
}

//----------------------------------------------------------------------
vtkMRMLTransformHandleWidgetRepresentation::TransformInteractionPipeline::TransformInteractionPipeline()
{
  this->NodeToHandleTransform = vtkSmartPointer<vtkTransform>::New();

  this->OutlineSource = vtkSmartPointer<vtkOutlineSource>::New();

  this->OutlineTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->OutlineTransformFilter->SetTransform(this->NodeToHandleTransform);
  this->OutlineTransformFilter->SetInputConnection(this->OutlineSource->GetOutputPort());

  this->ArrayCalculator = vtkSmartPointer<vtkArrayCalculator>::New();
  this->ArrayCalculator->SetInputConnection(this->OutlineTransformFilter->GetOutputPort());
  this->ArrayCalculator->SetAttributeTypeToPointData();
  this->ArrayCalculator->SetResultArrayName("colorIndex");
  this->ArrayCalculator->SetResultArrayType(VTK_FLOAT);
  this->ArrayCalculator->SetFunction("0");
}

//----------------------------------------------------------------------
vtkMRMLTransformHandleWidgetRepresentation::TransformInteractionPipeline::~TransformInteractionPipeline() = default;

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidgetRepresentation::SetupInteractionPipeline()
{
  this->Pipeline = new TransformInteractionPipeline();
  if (this->GetSliceNode())
    {
    this->Pipeline->WorldToSliceTransformFilter->SetInputConnection(this->Pipeline->HandleToWorldTransformFilter->GetOutputPort());
    this->Pipeline->WorldToSliceTransformFilter->SetTransform(this->WorldToSliceTransform);
    this->Pipeline->Mapper->SetInputConnection(this->Pipeline->WorldToSliceTransformFilter->GetOutputPort());
    this->Pipeline->Mapper->SetTransformCoordinate(nullptr);
    }

  TransformInteractionPipeline* pipeline = dynamic_cast<TransformInteractionPipeline*>(this->Pipeline);
  pipeline->Append->AddInputConnection(pipeline->ArrayCalculator->GetOutputPort());

  this->InitializePipeline();
  this->NeedToRenderOn();
}

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidgetRepresentation::UpdateHandleColors()
{
  int numberOfHandles = this->GetNumberOfHandles();
  this->Pipeline->ColorTable->SetNumberOfTableValues(numberOfHandles + 1);
  this->Pipeline->ColorTable->SetTableRange(0, double(numberOfHandles));

  int colorIndex = 0;

  // Outline color
  double outlineColor[4] = { 1.00, 1.00, 1.00, 1.00 };
  this->Pipeline->ColorTable->SetTableValue(colorIndex, outlineColor);

  colorIndex++;
  colorIndex = Superclass::UpdateHandleColors(InteractionRotationHandle, colorIndex);
  colorIndex = Superclass::UpdateHandleColors(InteractionTranslationHandle, colorIndex);
  colorIndex = Superclass::UpdateHandleColors(InteractionScaleHandle, colorIndex);

  this->Pipeline->ColorTable->Build();
}

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidgetRepresentation::SetDisplayNode(vtkMRMLTransformDisplayNode* displayNode)
{
  this->DisplayNode = displayNode;
}

//----------------------------------------------------------------------
vtkMRMLTransformNode* vtkMRMLTransformHandleWidgetRepresentation::GetTransformNode()
{
  if (!this->GetDisplayNode())
    {
    return nullptr;
    }
  return vtkMRMLTransformNode::SafeDownCast(this->GetDisplayNode()->GetDisplayableNode());
}

//----------------------------------------------------------------------
int vtkMRMLTransformHandleWidgetRepresentation::GetActiveComponentType()
{
  return this->DisplayNode->GetActiveInteractionType();
}

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidgetRepresentation::SetActiveComponentType(int type)
{
  this->DisplayNode->SetActiveInteractionType(type);
}

//----------------------------------------------------------------------
int vtkMRMLTransformHandleWidgetRepresentation::GetActiveComponentIndex()
{
  return this->DisplayNode->GetActiveInteractionIndex();
}

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidgetRepresentation::SetActiveComponentIndex(int index)
{
  this->DisplayNode->SetActiveInteractionIndex(index);
}

//----------------------------------------------------------------------
bool vtkMRMLTransformHandleWidgetRepresentation::IsDisplayable()
{
  if (!this->GetDisplayNode() || !this->GetTransformNode() || !this->GetTransformNode()->IsLinear())
    {
    return false;
    }
  return this->GetDisplayNode()->GetInteractionVisibility();
}

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidgetRepresentation::UpdateInteractionPipeline()
{
  vtkMRMLAbstractViewNode* viewNode = vtkMRMLAbstractViewNode::SafeDownCast(this->ViewNode);
  if (!viewNode || !this->GetTransformNode())
    {
    this->Pipeline->Actor->SetVisibility(false);
    return;
    }

  // We are specifying the position of the handles manually.
  this->Pipeline->AxisScaleGlypher->SetInputData(this->Pipeline->ScaleHandlePoints);

  // Final visibility handled by superclass in vtkMRMLInteractionWidgetRepresentation
  Superclass::UpdateInteractionPipeline();
}

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidgetRepresentation::UpdateHandleToWorldTransform(vtkTransform* handleToWorldTransform)
{
  vtkNew<vtkGeneralTransform> nodeToWorldTransform;
  vtkMRMLTransformNode::GetTransformBetweenNodes(this->GetTransformNode(), nullptr, nodeToWorldTransform);

  double centerOfTransformationNode[4] = { 0.0, 0.0, 0.0, 1.0 };
  this->GetTransformNode()->GetCenterOfTransformation(centerOfTransformationNode);
  double centerOfTransformationWorld[4] = { 0.0, 0.0, 0.0, 1.0 };
  nodeToWorldTransform->TransformPoint(centerOfTransformationNode, centerOfTransformationWorld);

  double xDirectionNode[4] = { 1.0, 0.0, 0.0, 0.0 };
  double xDirectionWorld[4] = { 1.0, 0.0, 0.0, 0.0 };
  nodeToWorldTransform->TransformVectorAtPoint(centerOfTransformationNode, xDirectionNode, xDirectionWorld);

  double yDirectionNode[4] = { 0.0, 1.0, 0.0, 0.0 };
  double yDirectionWorld[4] = { 0.0, 1.0, 0.0, 0.0 };
  nodeToWorldTransform->TransformVectorAtPoint(centerOfTransformationNode, yDirectionNode, yDirectionWorld);

  double zDirectionNode[4] = { 0.0, 0.0, 1.0, 0.0 };
  double zDirectionWorld[4] = { 0.0, 0.0, 1.0, 0.0 };
  nodeToWorldTransform->TransformVectorAtPoint(centerOfTransformationNode, zDirectionNode, zDirectionWorld);

  vtkNew<vtkMatrix4x4> nodeToWorldMatrix;
  for (int i = 0; i < 3; i++)
    {
    nodeToWorldMatrix->SetElement(i, 0, xDirectionWorld[i]);
    nodeToWorldMatrix->SetElement(i, 1, yDirectionWorld[i]);
    nodeToWorldMatrix->SetElement(i, 2, zDirectionWorld[i]);
    nodeToWorldMatrix->SetElement(i, 3, centerOfTransformationWorld[i]);
    }

  // The vtkMRMLInteractionWidgetRepresentation::UpdateHandleToWorldTransform() method will orthogonalize the matrix.
  handleToWorldTransform->Identity();
  handleToWorldTransform->PostMultiply();
  handleToWorldTransform->Concatenate(nodeToWorldMatrix);
}

//----------------------------------------------------------------------
double vtkMRMLTransformHandleWidgetRepresentation::GetInteractionScale()
{
  return this->GetDisplayNode()->GetInteractionScale();
}

//----------------------------------------------------------------------
double vtkMRMLTransformHandleWidgetRepresentation::GetInteractionSize()
{
  return this->GetDisplayNode()->GetInteractionSize();
}

//----------------------------------------------------------------------
bool vtkMRMLTransformHandleWidgetRepresentation::GetInteractionSizeAbsolute()
{
  return this->GetDisplayNode()->GetInteractionSizeAbsolute();
}

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidgetRepresentation::UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void* callData/*=nullptr*/)
{
  Superclass::UpdateFromMRML(caller, event, callData);

  TransformInteractionPipeline* pipeline = dynamic_cast<TransformInteractionPipeline*>(this->Pipeline);
  if (!pipeline)
    {
    return;
    }

  std::vector<vtkMRMLDisplayableNode*> transformedNodes;
  vtkMRMLTransformNode* transformNode = this->GetTransformNode();
  if (transformNode)
    {
    vtkSlicerTransformLogic::GetTransformedNodes(transformNode->GetScene(), transformNode, transformedNodes, true);
    }

  if (pipeline)
    {
    pipeline->NodeToHandleTransform->Identity();
    //pipeline->NodeToHandleTransform->PostMultiply();
    vtkNew<vtkMatrix4x4> worldToHandleTransform;
    pipeline->HandleToWorldTransform->GetInverse(worldToHandleTransform);
    vtkNew<vtkMatrix4x4> nodeToWorldTransform;
    transformNode->GetMatrixTransformToWorld(nodeToWorldTransform);
    pipeline->NodeToHandleTransform->Concatenate(worldToHandleTransform);
    pipeline->NodeToHandleTransform->Concatenate(nodeToWorldTransform);
    }

  bool validBounds = false;
  double bounds[6];
  if (transformedNodes.size() > 0)
    {
    vtkSlicerTransformLogic::GetNodesBounds(transformedNodes, bounds);
    validBounds =
      (bounds[0] <= bounds[1] || bounds[2] <= bounds[3] || bounds[4] <= bounds[5]);
    }

  pipeline->OutlineSource->SetBounds(bounds);

  vtkNew<vtkPoints> roiPoints;
  roiPoints->SetNumberOfPoints(14);
  int handleIndex = 0;
  roiPoints->SetPoint(handleIndex++, bounds[0], 0.0, 0.0);
  roiPoints->SetPoint(handleIndex++, bounds[1], 0.0, 0.0);
  roiPoints->SetPoint(handleIndex++, 0.0, bounds[2], 0.0);
  roiPoints->SetPoint(handleIndex++, 0.0, bounds[3], 0.0);
  roiPoints->SetPoint(handleIndex++, 0.0, 0.0, bounds[4]);
  roiPoints->SetPoint(handleIndex++, 0.0, 0.0, bounds[5]);
  roiPoints->SetPoint(handleIndex++, bounds[0], bounds[2], bounds[4]);
  roiPoints->SetPoint(handleIndex++, bounds[1], bounds[2], bounds[4]);
  roiPoints->SetPoint(handleIndex++, bounds[0], bounds[3], bounds[4]);
  roiPoints->SetPoint(handleIndex++, bounds[1], bounds[3], bounds[4]);
  roiPoints->SetPoint(handleIndex++, bounds[0], bounds[2], bounds[5]);
  roiPoints->SetPoint(handleIndex++, bounds[1], bounds[2], bounds[5]);
  roiPoints->SetPoint(handleIndex++, bounds[0], bounds[3], bounds[5]);
  roiPoints->SetPoint(handleIndex++, bounds[1], bounds[3], bounds[5]);
  this->Pipeline->ScaleHandlePoints->SetPoints(roiPoints);
}
