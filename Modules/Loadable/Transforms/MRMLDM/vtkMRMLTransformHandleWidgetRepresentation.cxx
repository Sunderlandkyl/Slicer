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
  Care Ontario, OpenAnatomy, and Brigham and Women�s Hospital through NIH grant R01MH112748.

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
