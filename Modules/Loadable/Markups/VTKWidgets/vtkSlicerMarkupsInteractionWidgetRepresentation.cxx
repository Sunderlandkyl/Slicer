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
#include <vtkMRMLMarkupsPlaneDisplayNode.h>
#include <vtkMRMLMarkupsPlaneNode.h>
#include <vtkMRMLMarkupsROIDisplayNode.h>
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

  vtkMRMLMarkupsPlaneNode* planeNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  if (planeNode && !planeNode->GetIsPlaneValid())
    {
    return false;
    }

  return this->GetDisplayNode()->GetVisibility() && this->GetDisplayNode()->GetHandlesInteractive();
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

  // We are specifying the position of the handles manually.
  this->Pipeline->AxisScaleGlypher->SetInputData(this->Pipeline->ScaleHandlePoints);

  // Final visibility handled by superclass in vtkMRMLInteractionWidgetRepresentation
  Superclass::UpdateInteractionPipeline();

  if (vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode()))
    {
    this->UpdatePlaneScaleHandles();
    }
  else if (vtkMRMLMarkupsROINode::SafeDownCast(this->GetMarkupsNode()))
    {
    this->UpdateROIScaleHandles();
    }
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::UpdatePlaneScaleHandles()
{
  vtkMRMLMarkupsPlaneNode* planeNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  if (!planeNode)
    {
    return;
    }

  vtkNew<vtkPoints> planeCornerPoints_World;
  planeNode->GetPlaneCornerPointsWorld(planeCornerPoints_World);

  double lpCorner_World[3] = { 0.0, 0.0, 0.0 };
  planeCornerPoints_World->GetPoint(0, lpCorner_World);

  double laCorner_World[3] = { 0.0, 0.0, 0.0 };
  planeCornerPoints_World->GetPoint(1, laCorner_World);

  double raCorner_World[3] = { 0.0, 0.0, 0.0 };
  planeCornerPoints_World->GetPoint(2, raCorner_World);

  double rpCorner_World[3] = { 0.0, 0.0, 0.0 };
  planeCornerPoints_World->GetPoint(3, rpCorner_World);

  double lEdge_World[3] = { 0.0, 0.0, 0.0 };
  vtkMath::Add(laCorner_World, lpCorner_World, lEdge_World);
  vtkMath::MultiplyScalar(lEdge_World, 0.5);

  double rEdge_World[3] = { 0.0, 0.0, 0.0 };
  vtkMath::Add(raCorner_World, rpCorner_World, rEdge_World);
  vtkMath::MultiplyScalar(rEdge_World, 0.5);

  double aEdge_World[3] = { 0.0, 0.0, 0.0 };
  vtkMath::Add(laCorner_World, raCorner_World, aEdge_World);
  vtkMath::MultiplyScalar(aEdge_World, 0.5);

  double pEdge_World[3] = { 0.0, 0.0, 0.0 };
  vtkMath::Add(lpCorner_World, rpCorner_World, pEdge_World);
  vtkMath::MultiplyScalar(pEdge_World, 0.5);

  vtkNew<vtkPoints> scaleHandlePoints;
  scaleHandlePoints->SetNumberOfPoints(8);
  scaleHandlePoints->SetPoint(vtkMRMLMarkupsPlaneDisplayNode::HandleLEdge, lEdge_World);
  scaleHandlePoints->SetPoint(vtkMRMLMarkupsPlaneDisplayNode::HandleREdge, rEdge_World);
  scaleHandlePoints->SetPoint(vtkMRMLMarkupsPlaneDisplayNode::HandleAEdge, aEdge_World);
  scaleHandlePoints->SetPoint(vtkMRMLMarkupsPlaneDisplayNode::HandlePEdge, pEdge_World);

  scaleHandlePoints->SetPoint(vtkMRMLMarkupsPlaneDisplayNode::HandleLPCorner, lpCorner_World);
  scaleHandlePoints->SetPoint(vtkMRMLMarkupsPlaneDisplayNode::HandleLACorner, laCorner_World);
  scaleHandlePoints->SetPoint(vtkMRMLMarkupsPlaneDisplayNode::HandleRACorner, raCorner_World);
  scaleHandlePoints->SetPoint(vtkMRMLMarkupsPlaneDisplayNode::HandleRPCorner, rpCorner_World);

  vtkNew<vtkTransform> worldToHandleTransform;
  worldToHandleTransform->DeepCopy(this->GetHandleToWorldTransform());
  worldToHandleTransform->Inverse();

  // Scale handles are expected in the handle coordinate system
  for (int i = 0; i < scaleHandlePoints->GetNumberOfPoints(); ++i)
    {
    double scaleHandlePoint[3] = { 0.0, 0.0, 0.0 };
    worldToHandleTransform->TransformPoint(scaleHandlePoints->GetPoint(i), scaleHandlePoint);
    scaleHandlePoints->SetPoint(i, scaleHandlePoint);
    }
  this->Pipeline->ScaleHandlePoints->SetPoints(scaleHandlePoints);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::UpdateROIScaleHandles()
{
  vtkMRMLMarkupsROINode* roiNode = vtkMRMLMarkupsROINode::SafeDownCast(this->GetMarkupsNode());
  if (!roiNode)
    {
    return;
    }

  double sideLengths[3] = { 0.0,  0.0, 0.0 };
  roiNode->GetSizeWorld(sideLengths);
  vtkMath::MultiplyScalar(sideLengths, 0.5);

  vtkNew<vtkPoints> roiPoints;
  roiPoints->SetNumberOfPoints(14);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleLFace, -sideLengths[0], 0.0, 0.0);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleRFace, sideLengths[0], 0.0, 0.0);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandlePFace, 0.0, -sideLengths[1], 0.0);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleAFace, 0.0, sideLengths[1], 0.0);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleIFace, 0.0, 0.0, -sideLengths[2]);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleSFace, 0.0, 0.0, sideLengths[2]);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleLPICorner, -sideLengths[0], -sideLengths[1], -sideLengths[2]);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleRPICorner, sideLengths[0], -sideLengths[1], -sideLengths[2]);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleLAICorner, -sideLengths[0], sideLengths[1], -sideLengths[2]);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleRAICorner, sideLengths[0], sideLengths[1], -sideLengths[2]);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleLPSCorner, -sideLengths[0], -sideLengths[1], sideLengths[2]);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleRPSCorner, sideLengths[0], -sideLengths[1], sideLengths[2]);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleLASCorner, -sideLengths[0], sideLengths[1], sideLengths[2]);
  roiPoints->SetPoint(vtkMRMLMarkupsROIDisplayNode::HandleRASCorner, sideLengths[0], sideLengths[1], sideLengths[2]);
  this->Pipeline->ScaleHandlePoints->SetPoints(roiPoints);
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
    return true;
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
void vtkSlicerMarkupsInteractionWidgetRepresentation::GetInteractionHandleAxisLocal(int type, int index, double axis_Local[3])
{
  if (!axis_Local)
    {
    vtkErrorWithObjectMacro(nullptr, "GetInteractionHandleVectorWorld: Invalid axis argument");
    return;
    }

  if (type != InteractionScaleHandle)
    {
    Superclass::GetInteractionHandleAxisLocal(type, index, axis_Local);
    return;
    }

  axis_Local[0] = 0.0;
  axis_Local[1] = 0.0;
  axis_Local[2] = 0.0;
  switch (index)
    {
    case 0:
    case 1:
      axis_Local[0] = 1.0;
      break;
    case 2:
    case 3:
      axis_Local[1] = 1.0;
      break;
    default:
      break;
    }

  if (vtkMRMLMarkupsROINode::SafeDownCast(this->GetMarkupsNode()))
    {
    switch (index)
      {
      case 4:
      case 5:
        axis_Local[2] = 1.0;
        break;
      default:
        break;
      }
    }
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

  if (vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode()))
    {
    switch (index)
      {
      case 0:
      case 1:
        currentColor = red;
        break;
      case 2:
      case 3:
        currentColor = green;
        break;
      default:
        break;
      }
    }
  else if (vtkMRMLMarkupsROINode::SafeDownCast(this->GetMarkupsNode()))
    {
    switch (index)
      {
      case 0:
      case 1:
        currentColor = red;
        break;
      case 2:
      case 3:
        currentColor = green;
        break;
      case 4:
      case 5:
        currentColor = blue;
        break;
      default:
        break;
      }
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

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidgetRepresentation::GetInteractionHandlePositionWorld(int type, int index, double positionWorld[3])
{
  if (!positionWorld)
    {
    vtkErrorWithObjectMacro(nullptr, "GetInteractionHandlePositionWorld: Invalid position argument");
    }

  if (type != InteractionScaleHandle)
    {
    Superclass::GetInteractionHandlePositionWorld(type, index, positionWorld);
    return;
    }

  vtkPolyData* handlePolyData = this->GetHandlePolydata(type);
  if (!handlePolyData)
    {
    return;
    }
  handlePolyData->GetPoint(index, positionWorld);
  this->Pipeline->HandleToWorldTransform->TransformPoint(positionWorld, positionWorld);
}