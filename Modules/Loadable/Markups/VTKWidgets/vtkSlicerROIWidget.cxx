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
    worldToROIMatrix->DeepCopy(markupsNode->GetInteractionHandleToWorldMatrix());
    worldToROIMatrix->Invert();
    vtkNew<vtkTransform> worldToROITransform;
    worldToROITransform->SetMatrix(worldToROIMatrix);

    int index = displayNode->GetActiveComponentIndex();
    if (index < 6 && rep3d)
      {
      this->GetClosestPointOnInteractionAxis(
        vtkMRMLMarkupsDisplayNode::ComponentScaleHandle, index, this->LastEventPosition, lastEventPos_World);
      this->GetClosestPointOnInteractionAxis(
        vtkMRMLMarkupsDisplayNode::ComponentScaleHandle, index, eventPos, eventPos_World);
      }

    double oldSideLengths[3] = { 0.0, 0.0, 0.0 };
    markupsNode->GetSideLengths(oldSideLengths);

    double scaleVector_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(eventPos_World, lastEventPos_World, scaleVector_World);
    if (index < 6 && rep2d)
      {
      double axis_World[3] = { 0.0, 0.0, 0.0 };
      markupsNode->GetAxisWorld(index/2, axis_World);
      vtkMath::ProjectVector(scaleVector_World, axis_World, axis_World);
      }

    double scaleVector_ROI[3] = { 0.0, 0.0, 0.0 };
    worldToROITransform->TransformVector(scaleVector_World, scaleVector_ROI);

    double bounds_ROI[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    markupsNode->GetBoundsROI(bounds_ROI);

    switch (index)
      {
      case vtkMRMLMarkupsROINode::L_FACE_POINT:
      case vtkMRMLMarkupsROINode::LAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPS_CORNER_POINT:
        bounds_ROI[0] += scaleVector_ROI[0];
        break;
      case vtkMRMLMarkupsROINode::R_FACE_POINT:
      case vtkMRMLMarkupsROINode::RAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPS_CORNER_POINT:
        bounds_ROI[1] += scaleVector_ROI[0];
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
        bounds_ROI[2] += scaleVector_ROI[1];
        break;
      case vtkMRMLMarkupsROINode::A_FACE_POINT:
      case vtkMRMLMarkupsROINode::LAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAI_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAS_CORNER_POINT:
        bounds_ROI[3] += scaleVector_ROI[1];
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
        bounds_ROI[4] += scaleVector_ROI[2];
        break;
      case vtkMRMLMarkupsROINode::S_FACE_POINT:
      case vtkMRMLMarkupsROINode::LAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RAS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::LPS_CORNER_POINT:
      case vtkMRMLMarkupsROINode::RPS_CORNER_POINT:
        bounds_ROI[5] += scaleVector_ROI[2];
        break;
      default:
        break;
      }

    double newSideLengths[3] = { 0.0, 0.0, 0.0 };
    double newOrigin_ROI[3] = { 0.0, 0.0, 0.0 };
    for (int i = 0; i < 3; ++i)
      {
      newSideLengths[i] = std::abs(bounds_ROI[2 * i + 1] - bounds_ROI[2 * i]);
      newOrigin_ROI[i] = (bounds_ROI[2 * i + 1] + bounds_ROI[2 * i]) / 2.0;
      }

    vtkNew<vtkTransform> roiToWorldTransform;
    roiToWorldTransform->SetMatrix(worldToROIMatrix);
    roiToWorldTransform->Inverse();

    double newOrigin_World[3] = { 0.0, 0.0, 0.0 };
    roiToWorldTransform->TransformPoint(newOrigin_ROI, newOrigin_World);
    markupsNode->SetOriginWorld(newOrigin_World);
    markupsNode->SetSideLengths(newSideLengths);

    bool flipLRHandle = bounds_ROI[1] < bounds_ROI[0];
    bool flipPAHandle = bounds_ROI[3] < bounds_ROI[2];
    bool flipISHandle = bounds_ROI[5] < bounds_ROI[4];

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