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

// VTK includes
#include <vtkPointData.h>

// Markups MRML includes
#include <vtkMRMLMarkupsPlaneNode.h>
#include <vtkMRMLMarkupsROINode.h>

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
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::UpdateHandleToWorldTransform(vtkTransform* handleToWorldTransform)
{
  handleToWorldTransform->Identity();
  handleToWorldTransform->Concatenate(this->GetMarkupsNode()->GetInteractionHandleToWorldMatrix());
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

  return Superclass::GetHandleVisibility(type, index);

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

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::CreateScaleHandles()
{
  double distance = 1.5;
  vtkMRMLMarkupsPlaneNode* planeNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  vtkMRMLMarkupsROINode* roiNode = vtkMRMLMarkupsROINode::SafeDownCast(this->GetMarkupsNode());
  if (planeNode || roiNode)
    {
    vtkNew<vtkPoints> points;
    points->InsertNextPoint(distance, 0.0, 0.0); // X-axis +ve
    points->InsertNextPoint(0.0, distance, 0.0); // Y-axis +ve
    points->InsertNextPoint(0.0, 0.0, distance); // Z-axis +ve
    points->InsertNextPoint(-distance, 0.0, 0.0); // X-axis -ve
    points->InsertNextPoint(0.0, -distance, 0.0); // Y-axis -ve
    points->InsertNextPoint(0.0, 0.0, -distance); // Z-axis -ve

    if (roiNode)
      {
      points->InsertNextPoint(distance, distance, distance);
      points->InsertNextPoint(distance, distance, -distance);
      points->InsertNextPoint(distance, -distance, distance);
      points->InsertNextPoint(distance, -distance, -distance);
      points->InsertNextPoint(-distance, distance, distance);
      points->InsertNextPoint(-distance, distance, -distance);
      points->InsertNextPoint(-distance, -distance, distance);
      points->InsertNextPoint(-distance, -distance, -distance);
      }
    else if (planeNode)
      {
      points->InsertNextPoint(distance, distance, 0.0);
      points->InsertNextPoint(-distance, distance, 0.0);
      points->InsertNextPoint(-distance, -distance, 0.0);
      points->InsertNextPoint(distance, -distance, 0.0);
      }

    this->Pipeline->ScaleHandlePoints->SetPoints(points);

    vtkNew<vtkIdTypeArray> visibilityArray;
    visibilityArray->SetName("visibility");
    visibilityArray->SetNumberOfComponents(1);
    visibilityArray->SetNumberOfValues(points->GetNumberOfPoints());
    visibilityArray->Fill(1.0);
    this->Pipeline->ScaleHandlePoints->GetPointData()->AddArray(visibilityArray);
    }
  else
    {
    Superclass::CreateScaleHandles();
    }
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::GetInteractionHandleAxisWorld(int type, int index, double axisWorld[3])
{
  if (!axisWorld)
    {
    vtkErrorWithObjectMacro(nullptr, "GetInteractionHandleVectorWorld: Invalid axis argument");
    return;
    }

  if (type != InteractionScaleHandle)
    {
    Superclass::GetInteractionHandleAxisWorld(type, index, axisWorld);
    return;
    }

  axisWorld[0] = 0.0;
  axisWorld[1] = 0.0;
  axisWorld[2] = 0.0;
  switch (index)
    {
    case 0:
    case 3:
      axisWorld[0] = 1.0;
      break;
    case 1:
    case 4:
      axisWorld[1] = 1.0;
      break;
    case 2:
    case 5:
      axisWorld[2] = 1.0;
      break;
    default:
      break;
    }

  double origin[3] = { 0.0, 0.0, 0.0 };
  this->Pipeline->HandleToWorldTransform->TransformVectorAtPoint(origin, axisWorld, axisWorld);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::GetHandleColor(int type, int index, double color[4])
{
  if (!color)
    {
    return;
    }
  if (type != InteractionScaleHandle)
    {
    Superclass::GetHandleColor(type, index, color);
    return;
    }

  double red[4]    = { 1.00, 0.00, 0.00, 1.00 };
  double green[4]  = { 0.00, 1.00, 0.00, 1.00 };
  double blue[4]   = { 0.00, 0.00, 1.00, 1.00 };
  double orange[4] = { 1.00, 0.50, 0.00, 1.00 };
  double white[4]  = { 1.00, 1.00, 1.00, 1.00 };
  double yellow[4] = { 1.00, 1.00, 0.00, 1.00 };

  double* currentColor = white;
  switch (index)
    {
    case 0:
    case 3:
      currentColor = red;
      break;
    case 1:
    case 4:
      currentColor = green;
      break;
    case 2:
    case 5:
      currentColor = blue;
      break;
    }
  if (index > 5)
    {
    currentColor = white;
    }

  double opacity = this->GetHandleOpacity(type, index);
  if (this->GetActiveComponentType() == type && this->GetActiveComponentIndex() == index)
    {
    currentColor = yellow;
    opacity = 1.0;
    }

  for (int i = 0; i < 3; ++i)
    {
    color[i] = currentColor[i];
    }
  color[3] = opacity;
}
