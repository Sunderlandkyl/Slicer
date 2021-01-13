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

#include "vtkSlicerROIWidget.h"

#include "vtkMRMLInteractionEventData.h"
#include "vtkMRMLMarkupsROINode.h"
#include "vtkMRMLSliceNode.h"
#include "vtkSlicerROIRepresentation2D.h"
#include "vtkSlicerROIRepresentation3D.h"

// VTK includes
#include <vtkCommand.h>
#include <vtkEvent.h>
#include <vtkPointPlacer.h>
#include <vtkRenderer.h>
#include <vtkTransform.h>

vtkStandardNewMacro(vtkSlicerROIWidget);

//----------------------------------------------------------------------
vtkSlicerROIWidget::vtkSlicerROIWidget()
{
  this->SetEventTranslationClickAndDrag(WidgetStateOnWidget, vtkCommand::LeftButtonPressEvent, vtkEvent::ShiftModifier,
    WidgetStateTranslatePlane, WidgetEventPlaneMoveStart, WidgetEventPlaneMoveEnd);
}

//----------------------------------------------------------------------
vtkSlicerROIWidget::~vtkSlicerROIWidget() = default;

//----------------------------------------------------------------------
void vtkSlicerROIWidget::CreateDefaultRepresentation(
  vtkMRMLMarkupsDisplayNode* markupsDisplayNode, vtkMRMLAbstractViewNode* viewNode, vtkRenderer* renderer)
{
  vtkSmartPointer<vtkSlicerMarkupsWidgetRepresentation> rep = nullptr;
  if (vtkMRMLSliceNode::SafeDownCast(viewNode))
    {
    rep = vtkSmartPointer<vtkSlicerROIRepresentation2D>::New();
    }
  else
    {
    rep = vtkSmartPointer<vtkSlicerROIRepresentation3D>::New();
    }
  this->SetRenderer(renderer);
  this->SetRepresentation(rep);
  rep->SetViewNode(viewNode);
  rep->SetMarkupsDisplayNode(markupsDisplayNode);
  rep->UpdateFromMRML(nullptr, 0); // full update
}

//----------------------------------------------------------------------
void vtkSlicerROIWidget::ScaleWidget(double eventPos[2])
{
  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
  vtkMRMLMarkupsROINode* markupsNode = vtkMRMLMarkupsROINode::SafeDownCast(this->GetMarkupsNode());
  if (!markupsNode || !displayNode)
    {
    return;
    }

  MRMLNodeModifyBlocker blocker(markupsNode);

  double lastEventPos_World[3] = { 0.0 };
  double eventPos_World[3] = { 0.0 };
  double orientation_World[9] = { 0.0 };

  vtkSlicerMarkupsWidgetRepresentation* rep = vtkSlicerMarkupsWidgetRepresentation::SafeDownCast(this->WidgetRep);
  vtkSlicerROIRepresentation2D* rep2d = vtkSlicerROIRepresentation2D::SafeDownCast(this->WidgetRep);
  vtkSlicerROIRepresentation3D* rep3d = vtkSlicerROIRepresentation3D::SafeDownCast(this->WidgetRep);
  if (rep2d)
    {
    // 2D view
    double eventPos_Slice[3] = { 0. };
    eventPos_Slice[0] = this->LastEventPosition[0];
    eventPos_Slice[1] = this->LastEventPosition[1];
    rep2d->GetSliceToWorldCoordinates(eventPos_Slice, lastEventPos_World);

    eventPos_Slice[0] = eventPos[0];
    eventPos_Slice[1] = eventPos[1];
    rep2d->GetSliceToWorldCoordinates(eventPos_Slice, eventPos_World);
    }
  else if (rep3d)
    {
    // 3D view
    double eventPos_Display[2] = { 0. };

    eventPos_Display[0] = this->LastEventPosition[0];
    eventPos_Display[1] = this->LastEventPosition[1];

    if (!rep3d->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      eventPos_Display, lastEventPos_World, eventPos_World,
      orientation_World))
      {
      return;
      }
    lastEventPos_World[0] = eventPos_World[0];
    lastEventPos_World[1] = eventPos_World[1];
    lastEventPos_World[2] = eventPos_World[2];

    eventPos_Display[0] = eventPos[0];
    eventPos_Display[1] = eventPos[1];

    if (!rep3d->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      eventPos_Display, lastEventPos_World, eventPos_World,
      orientation_World))
      {
      return;
      }
    }

  if (this->GetActiveComponentType() == vtkMRMLMarkupsDisplayNode::ComponentScaleHandle)
    {
    vtkNew<vtkMatrix4x4> worldToROIMatrix;
    worldToROIMatrix->DeepCopy(markupsNode->GetROIToWorldMatrix());
    worldToROIMatrix->Invert();
    vtkNew<vtkTransform> worldToROITransform;
    worldToROITransform->SetMatrix(worldToROIMatrix);

    int index = displayNode->GetActiveComponentIndex();

    double lastEventPos_ROI[3] = { 0.0, 0.0, 0.0 };
    if (index < 6)
      {
      double lastEventPositionOnAxis_World[3] = { 0.0, 0.0, 0.0 };
      this->GetClosestPointOnInteractionAxis(
        vtkMRMLMarkupsDisplayNode::ComponentScaleHandle, index, this->LastEventPosition, lastEventPositionOnAxis_World);
      worldToROITransform->TransformPoint(lastEventPositionOnAxis_World, lastEventPos_ROI);
      }
    else
      {
      worldToROITransform->TransformPoint(lastEventPos_World, lastEventPos_ROI);
      }

    double eventPos_ROI[3] = { 0.0, 0.0, 0.0 };
    if (index < 6)
      {
      double eventPositionOnAxis_World[3] = { 0.0, 0.0, 0.0 };
      this->GetClosestPointOnInteractionAxis(
        vtkMRMLMarkupsDisplayNode::ComponentScaleHandle, index, eventPos, eventPositionOnAxis_World);
      worldToROITransform->TransformPoint(eventPositionOnAxis_World, eventPos_ROI);
      }
    else
      {
      worldToROITransform->TransformPoint(eventPos_World, eventPos_ROI);
      }

    double scaleVector_ROI[3] = { 0.0, 0.0, 0.0 };

    double oldSideLengths[3] = { 0.0, 0.0, 0.0 };
    markupsNode->GetSideLengths(oldSideLengths);

    double bounds_ROI[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    markupsNode->GetBoundsROI(bounds_ROI);

    double axis_ROI[3] = { 0.0, 0.0, 0.0 };
    rep->GetInteractionHandleAxisWorld(vtkMRMLMarkupsDisplayNode::ComponentScaleHandle, index, axis_ROI);
    worldToROITransform->TransformVector(axis_ROI);
    vtkMath::Normalize(axis_ROI);
    switch (index)
      {
      case vtkMRMLMarkupsROINode::L_FACE_POINT:
      case vtkMRMLMarkupsROINode::R_FACE_POINT:
        eventPos_ROI[1] = 0.0;
        eventPos_ROI[2] = 0.0;
        break;
      case vtkMRMLMarkupsROINode::P_FACE_POINT:
      case vtkMRMLMarkupsROINode::A_FACE_POINT:
        eventPos_ROI[0] = 0.0;
        eventPos_ROI[2] = 0.0;
        break;
      case vtkMRMLMarkupsROINode::I_FACE_POINT:
      case vtkMRMLMarkupsROINode::S_FACE_POINT:
        eventPos_ROI[1] = 0.0;
        eventPos_ROI[0] = 0.0;
        break;
      default:
        break;
      }

    switch (index)
      {
      case vtkMRMLMarkupsROINode::L_FACE_POINT:
      case vtkMRMLMarkupsROINode::LAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPS_CORNER_POINT:
        bounds_ROI[0] = bounds_ROI[1];
        break;
      case vtkMRMLMarkupsROINode::R_FACE_POINT:
      case vtkMRMLMarkupsROINode::RAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPS_CORNER_POINT:
        bounds_ROI[1] = bounds_ROI[0];
        break;
      default:
        break;
      }

    switch (index)
      {
      case vtkMRMLMarkupsROINode::P_FACE_POINT:
      case vtkMRMLMarkupsROINode::LPI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPS_CORNER_POINT:
        bounds_ROI[2] = bounds_ROI[3];
        break;
      case vtkMRMLMarkupsROINode::A_FACE_POINT:
      case vtkMRMLMarkupsROINode::LAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAS_CORNER_POINT:
        bounds_ROI[3] = bounds_ROI[2];
        break;
      default:
        break;
      }

    switch (index)
      {
      case vtkMRMLMarkupsROINode::I_FACE_POINT:
      case vtkMRMLMarkupsROINode::LAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPI_CORNER_POINT:
        bounds_ROI[4] = bounds_ROI[5];
        break;
      case vtkMRMLMarkupsROINode::S_FACE_POINT:
      case vtkMRMLMarkupsROINode::LAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPS_CORNER_POINT:
        bounds_ROI[5] = bounds_ROI[4];
        break;
      default:
        break;
      }

    for (int i = 0; i < 3; ++i)
      {
      bounds_ROI[2 * i]     = std::min(eventPos_ROI[i], bounds_ROI[2 * i]);
      bounds_ROI[2 * i + 1] = std::max(eventPos_ROI[i], bounds_ROI[2 * i + 1]);
      }

    double newSideLengths[3] = { 0.0, 0.0, 0.0 };
    double newOrigin_ROI[3] = { 0.0, 0.0, 0.0 };
    for (int i = 0; i < 3; ++i)
      {
      newSideLengths[i] = std::abs(bounds_ROI[2 * i + 1] - bounds_ROI[2 * i]);
      newOrigin_ROI[i] = (bounds_ROI[2 * i + 1] + bounds_ROI[2 * i]) / 2.0;
      }
    markupsNode->SetSideLengths(newSideLengths);

    vtkNew<vtkTransform> roiToWorldTransform;
    worldToROITransform->SetMatrix(markupsNode->GetROIToWorldMatrix());

    double newOrigin_World[3] = { 0.0, 0.0, 0.0 };
    worldToROITransform->TransformPoint(newOrigin_ROI, newOrigin_World);
    markupsNode->SetOriginWorld(newOrigin_World);

    bool flipLRHandle = false;
    switch (index)
      {
      case vtkMRMLMarkupsROINode::L_FACE_POINT:
      case vtkMRMLMarkupsROINode::LAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPS_CORNER_POINT:
        flipLRHandle = (eventPos_ROI[0] > oldSideLengths[0] * 0.5);
        break;
      case vtkMRMLMarkupsROINode::R_FACE_POINT:
      case vtkMRMLMarkupsROINode::RAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPS_CORNER_POINT:
        flipLRHandle = (eventPos_ROI[0] < oldSideLengths[0] * -0.5);
        break;
      default:
        break;
      }

    bool flipPAHandle = false;
    switch (index)
      {
      case vtkMRMLMarkupsROINode::P_FACE_POINT:
      case vtkMRMLMarkupsROINode::LPI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPS_CORNER_POINT:
        flipPAHandle = (eventPos_ROI[1] > oldSideLengths[1] * 0.5);
        break;
      case vtkMRMLMarkupsROINode::A_FACE_POINT:
      case vtkMRMLMarkupsROINode::LAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAS_CORNER_POINT:
        flipPAHandle = (eventPos_ROI[1] < oldSideLengths[1] * -0.5);
        break;
      default:
        break;
      }

    bool flipISHandle = false;
    switch (index)
      {
      case vtkMRMLMarkupsROINode::I_FACE_POINT:
      case vtkMRMLMarkupsROINode::LAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPI_CORNER_POINT:
        flipISHandle = (eventPos_ROI[2] > oldSideLengths[2] * 0.5);
        break;
      case vtkMRMLMarkupsROINode::S_FACE_POINT:
      case vtkMRMLMarkupsROINode::LAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPS_CORNER_POINT:
        flipISHandle = (eventPos_ROI[2] < oldSideLengths[2] * -0.5);
        break;
      default:
        break;
      }

    if (flipLRHandle)
      {
      switch (index)
        {
        case vtkMRMLMarkupsROINode::L_FACE_POINT:
          index = vtkMRMLMarkupsROINode::R_FACE_POINT;
          break;
        case vtkMRMLMarkupsROINode::R_FACE_POINT:
          index = vtkMRMLMarkupsROINode::L_FACE_POINT;
          break;
        case vtkMRMLMarkupsROINode::LAI_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::RAI_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::LPI_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::RPI_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::LAS_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::RAS_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::LPS_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::RPS_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::RAI_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::LAI_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::RPI_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::LPI_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::RAS_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::LAS_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::RPS_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::LPS_CORNER_POINT;
          break;
        default:
          break;
        }
      }

    if (flipPAHandle)
      {
      switch (index)
        {
        case vtkMRMLMarkupsROINode::A_FACE_POINT:
          index = vtkMRMLMarkupsROINode::P_FACE_POINT;
          break;
        case vtkMRMLMarkupsROINode::P_FACE_POINT:
          index = vtkMRMLMarkupsROINode::A_FACE_POINT;
          break;
        case vtkMRMLMarkupsROINode::LAI_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::LPI_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::LPI_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::LAI_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::LAS_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::LPS_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::LPS_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::LAS_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::RAI_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::RPI_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::RPI_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::RAI_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::RAS_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::RPS_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::RPS_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::RAS_CORNER_POINT;
          break;
        default:
          break;
        }
      }

    if (flipISHandle)
      {
      switch (index)
        {
        case vtkMRMLMarkupsROINode::I_FACE_POINT:
          index = vtkMRMLMarkupsROINode::S_FACE_POINT;
          break;
        case vtkMRMLMarkupsROINode::S_FACE_POINT:
          index = vtkMRMLMarkupsROINode::I_FACE_POINT;
          break;
        case vtkMRMLMarkupsROINode::LAI_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::LAS_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::LPI_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::LPS_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::LAS_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::LAI_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::LPS_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::LPI_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::RAI_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::RAS_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::RPI_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::RPS_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::RAS_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::RAI_CORNER_POINT;
          break;
        case vtkMRMLMarkupsROINode::RPS_CORNER_POINT:
          index = vtkMRMLMarkupsROINode::RPI_CORNER_POINT;
          break;
        default:
          break;
        }
      }
    if (flipLRHandle || flipPAHandle || flipISHandle)
      {
      displayNode->SetActiveComponent(displayNode->GetActiveComponentType(), index);
      }
    }
}