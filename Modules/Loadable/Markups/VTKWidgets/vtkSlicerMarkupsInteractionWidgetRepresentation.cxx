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
#include "vtkSlicerMarkupsInteractionWidgetRepresentation.h"

// MRML includes
#include <vtkMRMLFolderDisplayNode.h>
#include <vtkMRMLInteractionEventData.h>
#include <vtkMRMLTransformNode.h>
#include <vtkMRMLViewNode.h>

//---------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerMarkupsInteractionWidgetRepresentation);

//----------------------------------------------------------------------
vtkSlicerMarkupsInteractionWidgetRepresentation::vtkSlicerMarkupsInteractionWidgetRepresentation()
{
}

//----------------------------------------------------------------------
vtkSlicerMarkupsInteractionWidgetRepresentation::~vtkSlicerMarkupsInteractionWidgetRepresentation() = default;

//-----------------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------
vtkMRMLMarkupsDisplayNode* vtkSlicerMarkupsInteractionWidgetRepresentation::GetDisplayNode()
{
  return this->DisplayNode;
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::SetDisplayNode(vtkMRMLMarkupsDisplayNode* displayNode)
{
  this->DisplayNode = displayNode;
}

//----------------------------------------------------------------------
vtkMRMLMarkupsNode* vtkSlicerMarkupsInteractionWidgetRepresentation::GetMarkupsNode()
{
  if (!this->GetDisplayNode())
    {
    return nullptr;
    }
  return vtkMRMLMarkupsNode::SafeDownCast(this->GetDisplayNode()->GetDisplayableNode());
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsInteractionWidgetRepresentation::GetActiveComponentType()
{
  return this->DisplayNode->GetActiveComponentType();
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::SetActiveComponentType(int type)
{
  //this->DisplayNode->SetActiveComponent(type);
  // TODO
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsInteractionWidgetRepresentation::GetActiveComponentIndex()
{
  return this->DisplayNode->GetActiveComponentIndex();
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::SetActiveComponentIndex(int index)
{
  //this->DisplayNode->SetActiveComponentIndex(index);
  // TODO
}

//----------------------------------------------------------------------
bool vtkSlicerMarkupsInteractionWidgetRepresentation::IsDisplayable()
{
  if (!this->GetDisplayNode() || !this->GetMarkupsNode())
    {
    return false;
    }
  return this->GetDisplayNode()->GetHandlesInteractive();
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::UpdateInteractionPipeline()
{
  vtkMRMLAbstractViewNode* viewNode = vtkMRMLAbstractViewNode::SafeDownCast(this->ViewNode);
  if (!viewNode || !this->GetMarkupsNode())
    {
    this->Pipeline->Actor->SetVisibility(false);
    return;
    }

  // Final visibility handled by superclass in vtkMRMLInteractionWidgetRepresentation
  Superclass::UpdateInteractionPipeline();

  this->Pipeline->HandleToWorldTransform->Identity();

  vtkMRMLDisplayableNode* displayableNode = this->DisplayNode->GetDisplayableNode();

  vtkNew<vtkMatrix4x4> nodeToWorld;
  vtkMRMLTransformNode::GetMatrixTransformBetweenNodes(this->GetMarkupsNode()->GetParentTransformNode(), nullptr, nodeToWorld);
  this->Pipeline->HandleToWorldTransform->Concatenate(nodeToWorld);

  //double centerOfTransformation[3] = { 0.0, 0.0, 0.0 };
  //this->GetMarkupsNode()->GetCenterOfTransformation(centerOfTransformation);
  //this->Pipeline->HandleToWorldTransform->Translate(centerOfTransformation);
}

//----------------------------------------------------------------------
double vtkSlicerMarkupsInteractionWidgetRepresentation::GetInteractionScale()
{
  return this->GetDisplayNode()->GetGlyphScale();
}

//----------------------------------------------------------------------
double vtkSlicerMarkupsInteractionWidgetRepresentation::GetInteractionSize()
{
  return this->GetDisplayNode()->GetGlyphSize();
}

//----------------------------------------------------------------------
bool vtkSlicerMarkupsInteractionWidgetRepresentation::GetInteractionSizeAbsolute()
{
  return !this->GetDisplayNode()->GetUseGlyphScale();
}
