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
#include "vtkMatrix4x4.h"

// Transform MRMLDM includes
#include "vtkMRMLTransformHandleWidgetRepresentation.h"

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
  handleToWorldTransform->Identity();
  handleToWorldTransform->PostMultiply();

  vtkMRMLDisplayableNode* displayableNode = this->DisplayNode->GetDisplayableNode();

  vtkNew<vtkMatrix4x4> nodeToWorld;
  vtkMRMLTransformNode::GetMatrixTransformBetweenNodes(this->GetTransformNode(), nullptr, nodeToWorld);

  double centerOfTransformation[3] = { 0.0, 0.0, 0.0 };
  this->GetTransformNode()->GetCenterOfTransformation(centerOfTransformation);
  handleToWorldTransform->Translate(centerOfTransformation);
  handleToWorldTransform->Concatenate(nodeToWorld);
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
