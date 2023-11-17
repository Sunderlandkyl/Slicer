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

// Transform MRMLDM includes
#include "vtkSlicerMarkupsInteractionWidgetRepresentation.h"

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
int vtkSlicerMarkupsInteractionWidgetRepresentation::InteractionComponentToMarkupsComponent(int interactionComponentType)
{
  int markupsComponentType = vtkMRMLMarkupsDisplayNode::ComponentNone;
  if (interactionComponentType == InteractionRotationHandle)
    {
    markupsComponentType = vtkMRMLMarkupsDisplayNode::ComponentRotationHandle;
    }
  else if (interactionComponentType == InteractionScaleHandle)
    {
    markupsComponentType = vtkMRMLMarkupsDisplayNode::ComponentScaleHandle;
    }
  else if (interactionComponentType == InteractionTranslationHandle)
    {
    markupsComponentType = vtkMRMLMarkupsDisplayNode::ComponentTranslationHandle;
    }
  return markupsComponentType;
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsInteractionWidgetRepresentation::MarkupsComponentToInteractionComponent(int markupsComponentType)
{
  int interactionComponentType = InteractionNone;
  if (markupsComponentType == vtkMRMLMarkupsDisplayNode::ComponentRotationHandle)
    {
    interactionComponentType = InteractionRotationHandle;
    }
  else if (markupsComponentType == vtkMRMLMarkupsDisplayNode::ComponentScaleHandle)
    {
    interactionComponentType = InteractionScaleHandle;
    }
  else if (markupsComponentType == vtkMRMLMarkupsDisplayNode::ComponentTranslationHandle)
    {
    interactionComponentType = InteractionTranslationHandle;
    }
  return interactionComponentType;
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsInteractionWidgetRepresentation::GetActiveComponentType()
{
  int activeComponentType = this->DisplayNode->GetActiveComponentType();
  return this->MarkupsComponentToInteractionComponent(activeComponentType);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::SetActiveComponentType(int type)
{
  int markupsComponentType = this->InteractionComponentToMarkupsComponent(type);
  this->DisplayNode->SetActiveComponent(markupsComponentType, this->GetActiveComponentIndex());
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsInteractionWidgetRepresentation::GetActiveComponentIndex()
{
  return this->DisplayNode->GetActiveComponentIndex();
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::SetActiveComponentIndex(int index)
{
  this->DisplayNode->SetActiveComponent(this->DisplayNode->GetActiveComponentType(), index);
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
  this->Pipeline->HandleToWorldTransform->Concatenate(this->GetMarkupsNode()->GetInteractionHandleToWorldMatrix());
}

//----------------------------------------------------------------------
double vtkSlicerMarkupsInteractionWidgetRepresentation::GetInteractionScale()
{
  return this->GetDisplayNode()->GetGlyphScale() * 5.0;
}

//----------------------------------------------------------------------
double vtkSlicerMarkupsInteractionWidgetRepresentation::GetInteractionSize()
{
  return this->GetDisplayNode()->GetGlyphSize() * 5.0;
}

//----------------------------------------------------------------------
bool vtkSlicerMarkupsInteractionWidgetRepresentation::GetInteractionSizeAbsolute()
{
  return !this->GetDisplayNode()->GetUseGlyphScale();
}

//----------------------------------------------------------------------
bool vtkSlicerMarkupsInteractionWidgetRepresentation::GetHandleVisibility(int type, int index)
{
  if (!this->GetDisplayNode())
    {
    return false;
    }

  int markupsComponentType = this->InteractionComponentToMarkupsComponent(type);
  if (!this->GetDisplayNode()->GetHandleVisibility(markupsComponentType))
    {
    return false;
    }

  bool handleVisibility[4] = { false, false, false, false };
  if (markupsComponentType == vtkMRMLMarkupsDisplayNode::ComponentRotationHandle)
    {
    this->GetDisplayNode()->GetRotationHandleComponentVisibility(handleVisibility);
    }
  else if (markupsComponentType == vtkMRMLMarkupsDisplayNode::ComponentScaleHandle)
    {
    this->GetDisplayNode()->GetScaleHandleComponentVisibility(handleVisibility);
    }
  else if (markupsComponentType == vtkMRMLMarkupsDisplayNode::ComponentTranslationHandle)
    {
    this->GetDisplayNode()->GetTranslationHandleComponentVisibility(handleVisibility);
    }

  if (index < 0 || index > 3)
    {
    return false;
    }

  return handleVisibility[index];
}
