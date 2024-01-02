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
#include <vtkAppendPolyData.h>
#include <vtkCamera.h>
#include <vtkCellData.h>
#include <vtkDoubleArray.h>
#include <vtkEllipseArcSource.h>
#include <vtkFloatArray.h>
#include <vtkFocalPlanePointPlacer.h>
#include <vtkGlyph3D.h>
#include <vtkLine.h>
#include <vtkLineSource.h>
#include <vtkLookupTable.h>
#include <vtkMRMLSliceNode.h>
#include <vtkMRMLViewNode.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPointSetToLabelHierarchy.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkProperty2D.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkSphereSource.h>
#include <vtkStringArray.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>
#include <vtkTensorGlyph.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkTriangleFilter.h>
#include <vtkTubeFilter.h>

// MRML includes
#include <vtkMRMLFolderDisplayNode.h>
#include <vtkMRMLInteractionEventData.h>
#include <vtkMRMLTransformNode.h>

#include <vtkMRMLInteractionWidgetRepresentation.h>

//----------------------------------------------------------------------
static const double INTERACTION_HANDLE_SCALE_RADIUS = 0.1;

static const double INTERACTION_HANDLE_ROTATION_ARC_OUTER_RADIUS = 1.2;
static const double INTERACTION_HANDLE_ROTATION_ARC_INNER_RADIUS = 1.1;
static const double INTERACTION_HANDLE_ROTATION_ARC_DEGREES = 360.0;
static const int    INTERACTION_HANDLE_ROTATION_ARC_RESOLUTION = 30;

static const double INTERACTION_TRANSLATION_HANDLE_LENGTH= 0.75;
static const double INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS = 0.15;
static const double INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS = 0.05;

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::vtkMRMLInteractionWidgetRepresentation()
{
  this->ViewScaleFactorMmPerPixel = 1.0;
  this->ScreenSizePixel = 1000;

  this->NeedToRender = false;

  this->PointPlacer = vtkSmartPointer<vtkFocalPlanePointPlacer>::New();

  this->AlwaysOnTop = true;

  this->Pipeline = nullptr;

  this->SlicePlane = vtkSmartPointer<vtkPlane>::New();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::SetupInteractionPipeline()
{
  this->Pipeline = new InteractionPipeline();
  this->InitializePipeline();
  if (this->GetSliceNode())
    {
    this->Pipeline->WorldToSliceTransformFilter->SetInputConnection(this->Pipeline->HandleToWorldTransformFilter->GetOutputPort());
    this->Pipeline->Mapper3D->SetInputConnection(this->Pipeline->WorldToSliceTransformFilter->GetOutputPort());
    }
  this->NeedToRenderOn();
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::~vtkMRMLInteractionWidgetRepresentation()
{
  // Force deleting variables to prevent circular dependency keeping objects alive
  if (this->Pipeline != nullptr)
    {
    delete this->Pipeline;
    this->Pipeline = nullptr;
    }
}

//-----------------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::PrintSelf(ostream& os,
                                                      vtkIndent indent)
{
  //Superclass typedef defined in vtkTypeMacro() found in vtkSetGet.h
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetMaximumHandlePickingDistance2()
{
  double maximumHandlePickingDistance = this->InteractionSize / 2.0 + this->PickingTolerance * this->ScreenScaleFactor;
  return maximumHandlePickingDistance * maximumHandlePickingDistance;
}

//-----------------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CanInteract(
  vtkMRMLInteractionEventData* interactionEventData,
  int &foundComponentType, int &foundComponentIndex, double &closestDistance2)
{
  foundComponentType = InteractionNone;
  vtkMRMLAbstractViewNode* viewNode = this->GetViewNode();
  if (!viewNode || !this->IsDisplayable() || !interactionEventData)
    {
    return;
    }

  closestDistance2 = VTK_DOUBLE_MAX; // in display coordinate system
  foundComponentIndex = -1;

  double maxPickingDistanceFromControlPoint2 = this->GetMaximumHandlePickingDistance2();
  double displayPosition3[3] = { 0.0, 0.0, 0.0 };
  // Display position is valid in case of desktop interactions. Otherwise it is a 3D only context such as
  // virtual reality, and then we expect a valid world position in the absence of display position.
  if (interactionEventData->IsDisplayPositionValid())
    {
    const int* displayPosition = interactionEventData->GetDisplayPosition();
    displayPosition3[0] = static_cast<double>(displayPosition[0]);
    displayPosition3[1] = static_cast<double>(displayPosition[1]);
    }
  else if (!interactionEventData->IsWorldPositionValid())
    {
    return;
    }

  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (sliceNode)
    {
    double handleDisplayPos[4] = { 0.0, 0.0, 0.0, 1.0 };

    vtkNew<vtkMatrix4x4> rasToxyMatrix;
    vtkMatrix4x4::Invert(sliceNode->GetXYToRAS(), rasToxyMatrix);

    bool handlePicked = false;
    HandleInfoList handleInfoList = this->GetHandleInfoList();
    for (HandleInfo handleInfo : handleInfoList)
      {
      if (!handleInfo.IsVisible())
        {
        continue;
        }
      double* handleWorldPos = handleInfo.PositionWorld;
      rasToxyMatrix->MultiplyPoint(handleWorldPos, handleDisplayPos);
      handleDisplayPos[2] = displayPosition3[2]; // Handles are always projected
      double dist2 = vtkMath::Distance2BetweenPoints(handleDisplayPos, displayPosition3);
      if (dist2 < maxPickingDistanceFromControlPoint2 && dist2 < closestDistance2)
        {
        closestDistance2 = dist2;
        foundComponentType = handleInfo.ComponentType;
        foundComponentIndex = handleInfo.Index;
        handlePicked = true;
        }
      }

    if (!handlePicked)
      {
      // Detect translation handle shaft
      for (HandleInfo handleInfo : handleInfoList)
        {
        if (!handleInfo.IsVisible() || handleInfo.ComponentType != InteractionTranslationHandle)
          {
          continue;
          }

        double* handleWorldPos = handleInfo.PositionWorld;
        rasToxyMatrix->MultiplyPoint(handleWorldPos, handleDisplayPos);
        handleDisplayPos[2] = displayPosition3[2]; // Handles are always projected

        double originWorldPos[4] = { 0.0, 0.0, 0.0, 1.0 };
        this->GetInteractionHandleOriginWorld(originWorldPos);
        double originDisplayPos[4] = { 0.0, 0.0, 0.0, 0.0 };
        rasToxyMatrix->MultiplyPoint(originWorldPos, originDisplayPos);
        originDisplayPos[2] = displayPosition3[2]; // Handles are always projected

        double t = 0.0; // Not used
        double lineDistance = vtkLine::DistanceToLine(displayPosition3, originDisplayPos, handleDisplayPos, t);
        double lineDistance2 = lineDistance * lineDistance;
        if (lineDistance2 < maxPickingDistanceFromControlPoint2 / 2.0 && lineDistance2 < closestDistance2)
          {
          closestDistance2 = lineDistance2;
          foundComponentType = handleInfo.ComponentType;
          foundComponentIndex = handleInfo.Index;
          }
        }
      }
    }
  else
    {

    bool handlePicked = false;
    HandleInfoList handleInfoList = this->GetHandleInfoList();
    for (HandleInfo handleInfo : handleInfoList)
      {
      if (!handleInfo.IsVisible())
        {
        continue;
        }

      double* handleWorldPos = handleInfo.PositionWorld;
      double handleDisplayPos[3] = { 0.0, 0.0, 0.0 };

      double pixelTolerance = this->InteractionSize / 4.0 / this->GetViewScaleFactorAtPosition(handleWorldPos)
        + this->PickingTolerance * this->ScreenScaleFactor;
      double dist2 = VTK_DOUBLE_MAX;

      if (interactionEventData->IsDisplayPositionValid())
        {
        if (handleInfo.ComponentType == InteractionRotationHandle)
          {
          double handleNormalWorld[4] = { 0.0, 0.0, 0.0, 0.0 };
          this->GetInteractionHandleAxisWorld(handleInfo.ComponentType, handleInfo.Index, handleNormalWorld);
          if (handleNormalWorld[0] <= 0.0 && handleNormalWorld[1] <= 0.0 && handleNormalWorld[2] <= 0.0)
            {
            // Vector from camera position to handle position.
            double cameraWorld[3] = { 0.0, 0.0, 0.0 };
            this->Renderer->GetActiveCamera()->GetPosition(cameraWorld);
            vtkMath::Subtract(handleWorldPos, cameraWorld, handleNormalWorld);
            }

          vtkNew<vtkPlane> plane;
          plane->SetOrigin(handleWorldPos);
          plane->SetNormal(handleNormalWorld);

          this->Renderer->SetDisplayPoint(displayPosition3);
          this->Renderer->DisplayToWorld();
          double worldPosition[4] = { 0.0, 0.0, 0.0, 0.0 };
          this->Renderer->GetWorldPoint(worldPosition);

          double viewDirection_World[3] = { 0.0, 0.0, 0.0 };
          this->Renderer->GetActiveCamera()->GetDirectionOfProjection(viewDirection_World);

          double worldPosition2[4] = { 0.0, 0.0, 0.0, 0.0 };
          vtkMath::Add(worldPosition, viewDirection_World, worldPosition2);

          double t = 0; // not used
          double interactionPointOnPlaneWorld[3] = { 0.0, 0.0, 0.0 };
          plane->IntersectWithLine(worldPosition, this->Renderer->GetActiveCamera()->GetPosition(), t, interactionPointOnPlaneWorld);

          double closestPointOnRingWorld[3] = { 0.0, 0.0, 0.0 };
          vtkMath::Subtract(interactionPointOnPlaneWorld, handleWorldPos, closestPointOnRingWorld);
          vtkMath::Normalize(closestPointOnRingWorld);
          vtkMath::MultiplyScalar(closestPointOnRingWorld, this->WidgetScale);
          vtkMath::Add(handleWorldPos, closestPointOnRingWorld, closestPointOnRingWorld);

          double closestPointOnRingDisplay[3] = { 0.0, 0.0, 0.0 };
          this->Renderer->SetWorldPoint(closestPointOnRingWorld);
          this->Renderer->WorldToDisplay();
          this->Renderer->GetDisplayPoint(closestPointOnRingDisplay);

          dist2 = vtkMath::Distance2BetweenPoints(displayPosition3, closestPointOnRingDisplay);
          }
        else
          {
          this->Renderer->SetWorldPoint(handleWorldPos);
          this->Renderer->WorldToDisplay();
          this->Renderer->GetDisplayPoint(handleDisplayPos);
          handleDisplayPos[2] = 0.0;
          dist2 = vtkMath::Distance2BetweenPoints(handleDisplayPos, displayPosition3);
          }

        if (dist2 < pixelTolerance * pixelTolerance && dist2 < closestDistance2)
          {
          closestDistance2 = dist2;
          foundComponentType = handleInfo.ComponentType;
          foundComponentIndex = handleInfo.Index;
          handlePicked = true;
          }
        }
      else
        {
        const double* worldPosition = interactionEventData->GetWorldPosition();
        double worldTolerance = this->InteractionSize / 2.0 +
          this->PickingTolerance / interactionEventData->GetWorldToPhysicalScale();
        double dist2 = vtkMath::Distance2BetweenPoints(handleWorldPos, worldPosition);
        if (dist2 < worldTolerance * worldTolerance && dist2 < closestDistance2)
          {
          closestDistance2 = dist2;
          foundComponentType = handleInfo.ComponentType;
          foundComponentIndex = handleInfo.Index;
          }
        }
      }

    if (!handlePicked)
      {
      // Detect translation handle shaft
      for (HandleInfo handleInfo : handleInfoList)
        {
        if (!handleInfo.IsVisible() || handleInfo.ComponentType != InteractionTranslationHandle)
          {
          continue;
          }
        double* handleWorldPos = handleInfo.PositionWorld;
        double handleDisplayPos[3] = { 0.0, 0.0, 0.0 };

        if (interactionEventData->IsDisplayPositionValid())
          {
          double pixelTolerance = this->InteractionSize / 2.0 / this->GetViewScaleFactorAtPosition(handleWorldPos)
            + this->PickingTolerance * this->ScreenScaleFactor;
          this->Renderer->SetWorldPoint(handleWorldPos);
          this->Renderer->WorldToDisplay();
          this->Renderer->GetDisplayPoint(handleDisplayPos);
          handleDisplayPos[2] = 0.0;

          double originWorldPos[4] = { 0.0, 0.0, 0.0, 1.0 };
          this->GetInteractionHandleOriginWorld(originWorldPos);
          double originDisplayPos[4] = { 0.0, 0.0, 0.0, 0.0 };
          this->Renderer->SetWorldPoint(originWorldPos);
          this->Renderer->WorldToDisplay();
          this->Renderer->GetDisplayPoint(originDisplayPos);
          originDisplayPos[2] = displayPosition3[2]; // Handles are always projected
          double t = 0.0; // Not used
          double lineDistance = vtkLine::DistanceToLine(displayPosition3, originDisplayPos, handleDisplayPos, t);
          double lineDistance2 = lineDistance * lineDistance;
          if (lineDistance < pixelTolerance && lineDistance2 < closestDistance2)
            {
            closestDistance2 = lineDistance2;
            foundComponentType = handleInfo.ComponentType;
            foundComponentIndex = handleInfo.Index;
            }
          }
        else
          {
          const double* worldPosition = interactionEventData->GetWorldPosition();
          double worldTolerance = this->InteractionSize / 2.0 +
            this->PickingTolerance / interactionEventData->GetWorldToPhysicalScale();
          double originWorldPos[4] = { 0.0, 0.0, 0.0, 1.0 };
          this->GetInteractionHandleOriginWorld(originWorldPos);
          double t = 0.0; // Not used
          double lineDistance = vtkLine::DistanceToLine(worldPosition, originWorldPos, handleWorldPos, t);
          if (lineDistance < worldTolerance && lineDistance < closestDistance2)
            {
            closestDistance2 = lineDistance;
            foundComponentType = handleInfo.ComponentType;
            foundComponentIndex = handleInfo.Index;
            }
          }
        }
      }
    }
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetViewScaleFactorAtPosition(double positionWorld[3])
{
  double viewScaleFactorMmPerPixel = 1.0;
  if (!this->Renderer || !this->Renderer->GetActiveCamera())
    {
    return viewScaleFactorMmPerPixel;
    }

  vtkCamera * cam = this->Renderer->GetActiveCamera();
  if (cam->GetParallelProjection())
    {
    // Viewport: xmin, ymin, xmax, ymax; range: 0.0-1.0; origin is bottom left
    // Determine the available renderer size in pixels
    double minX = 0;
    double minY = 0;
    this->Renderer->NormalizedDisplayToDisplay(minX, minY);
    double maxX = 1;
    double maxY = 1;
    this->Renderer->NormalizedDisplayToDisplay(maxX, maxY);
    int rendererSizeInPixels[2] = { static_cast<int>(maxX - minX), static_cast<int>(maxY - minY) };
    // Parallel scale: height of the viewport in world-coordinate distances.
    // Larger numbers produce smaller images.
    viewScaleFactorMmPerPixel = (cam->GetParallelScale() * 2.0) / double(rendererSizeInPixels[1]);
    }
  else
    {
    double cameraFP[4] = { positionWorld[0], positionWorld[1], positionWorld[2], 1.0 };

    double cameraViewUp[3] = { 0.0, 0.0, 0.0 };
    cam->GetViewUp(cameraViewUp);
    vtkMath::Normalize(cameraViewUp);

    // Get distance in pixels between two points at unit distance above and below the focal point
    this->Renderer->SetWorldPoint(cameraFP[0] + cameraViewUp[0], cameraFP[1] + cameraViewUp[1], cameraFP[2] + cameraViewUp[2], cameraFP[3]);
    this->Renderer->WorldToDisplay();
    double topCenter[3] = { 0.0, 0.0, 0.0 };
    this->Renderer->GetDisplayPoint(topCenter);
    topCenter[2] = 0.0;
    this->Renderer->SetWorldPoint(cameraFP[0] - cameraViewUp[0], cameraFP[1] - cameraViewUp[1], cameraFP[2] - cameraViewUp[2], cameraFP[3]);
    this->Renderer->WorldToDisplay();
    double bottomCenter[3] = { 0.0, 0.0, 0.0 };
    this->Renderer->GetDisplayPoint(bottomCenter);
    bottomCenter[2] = 0.0;
    double distInPixels = sqrt(vtkMath::Distance2BetweenPoints(topCenter, bottomCenter));

    // if render window is not initialized yet then distInPixels == 0.0,
    // in that case just leave the default viewScaleFactorMmPerPixel
    if (distInPixels > 1e-3)
      {
      // 2.0 = 2x length of viewUp vector in mm (because viewUp is unit vector)
      viewScaleFactorMmPerPixel = 2.0 / distInPixels;
      }
    }
  return viewScaleFactorMmPerPixel;
}

//----------------------------------------------------------------------
bool vtkMRMLInteractionWidgetRepresentation::GetTransformationReferencePoint(double referencePointWorld[3])
{
  double origin[3] = { 0.0, 0.0, 0.0 };
  this->GetHandleToWorldTransform()->TransformPoint(origin, referencePointWorld);
  return true;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateFromMRML(
    vtkMRMLNode* vtkNotUsed(caller), unsigned long event, void *vtkNotUsed(callData))
{
  if (!this->Pipeline)
    {
    this->SetupInteractionPipeline();
    }

  if (this->GetSliceNode())
    {
    this->UpdateSlicePlaneFromSliceNode();
    }

  if (this->Pipeline)
    {
    this->UpdateInteractionPipeline();
    }

  this->NeedToRenderOn();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateInteractionPipeline()
{
  this->NeedToRenderOn();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateHandleToWorldTransform()
{
  vtkTransform* handleToWorldTransform = this->GetHandleToWorldTransform();
  this->UpdateHandleToWorldTransform(handleToWorldTransform);
  this->OrthoganalizeTransform(handleToWorldTransform);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::OrthoganalizeTransform(vtkTransform* transform)
{
  double x[3] = { 1.0, 0.0, 0.0 };
  double y[3] = { 0.0, 1.0, 0.0 };
  double z[3] = { 0.0, 0.0, 1.0 };

  transform->TransformVector(x, x);
  transform->TransformVector(y, y);
  transform->TransformVector(z, z);
  vtkMath::Normalize(x);
  vtkMath::Normalize(y);
  vtkMath::Normalize(z);

  double xOrthogonal[3] = { 1.0, 0.0, 0.0 };
  double yOrthogonal[3] = { 0.0, 1.0, 0.0 };
  double zOrthogonal[3] = { z[0], z[1], z[2] };
  vtkMath::Cross(zOrthogonal, x, yOrthogonal);
  vtkMath::Normalize(yOrthogonal);
  vtkMath::Cross(yOrthogonal, zOrthogonal, xOrthogonal);
  vtkMath::Normalize(xOrthogonal);

  vtkNew<vtkMatrix4x4> orthogonalMatrix;
  orthogonalMatrix->DeepCopy(transform->GetMatrix());
  for (int i = 0; i < 3; ++i)
    {
    orthogonalMatrix->SetElement(i, 0, xOrthogonal[i]);
    orthogonalMatrix->SetElement(i, 1, yOrthogonal[i]);
    orthogonalMatrix->SetElement(i, 2, zOrthogonal[i]);
    }

  transform->Identity();
  transform->Concatenate(orthogonalMatrix);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateHandlePolyData()
{
  this->Pipeline->Append->RemoveAllInputs();

  vtkNew<vtkPolyData> arrowPoints;
  vtkNew<vtkPolyData> arrowOutlinePoints;
  vtkNew<vtkPolyData> circlePoints;
  vtkNew<vtkPolyData> circleOutlinePoints;
  vtkNew<vtkPolyData> ringPoints;
  vtkNew<vtkPolyData> ringOutlinePoints;

  vtkNew<vtkTensorGlyph> arrowGlyph;
  arrowGlyph->SetSourceData(this->Pipeline->ArrowPolyData);
  arrowGlyph->SetInputData(arrowPoints);
  arrowGlyph->SetScaleFactor(this->WidgetScale);
  arrowGlyph->ScalingOff();
  arrowGlyph->ExtractEigenvaluesOff();
  arrowGlyph->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  vtkNew<vtkTensorGlyph> arrowOutlineGlyph;
  arrowOutlineGlyph->SetSourceData(this->Pipeline->ArrowOutlinePolyData);
  arrowOutlineGlyph->SetInputData(arrowOutlinePoints);
  arrowOutlineGlyph->SetScaleFactor(this->WidgetScale);
  arrowOutlineGlyph->ScalingOff();
  arrowOutlineGlyph->ExtractEigenvaluesOff();
  arrowOutlineGlyph->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  vtkNew<vtkTensorGlyph> circleGlyph;
  circleGlyph->SetSourceData(this->Pipeline->CirclePolyData);
  circleGlyph->SetInputData(circlePoints);
  circleGlyph->SetScaleFactor(this->WidgetScale);
  circleGlyph->ScalingOff();
  circleGlyph->ExtractEigenvaluesOff();
  circleGlyph->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  vtkNew<vtkTensorGlyph> circleOutlineGlyph;
  circleOutlineGlyph->SetSourceData(this->Pipeline->CircleOutlinePolyData);
  circleOutlineGlyph->SetInputData(circleOutlinePoints);
  circleOutlineGlyph->SetScaleFactor(this->WidgetScale);
  circleOutlineGlyph->ScalingOff();
  circleOutlineGlyph->ExtractEigenvaluesOff();
  circleOutlineGlyph->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  vtkNew<vtkTensorGlyph> ringGlyph;
  ringGlyph->SetSourceData(this->Pipeline->RingPolyData);
  ringGlyph->SetInputData(ringPoints);
  ringGlyph->SetScaleFactor(this->WidgetScale);
  ringGlyph->ScalingOff();
  ringGlyph->ExtractEigenvaluesOff();
  ringGlyph->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  vtkNew<vtkTensorGlyph> ringOutlineGlyph;
  ringOutlineGlyph->SetSourceData(this->Pipeline->RingOutlinePolyData);
  ringOutlineGlyph->SetInputData(ringOutlinePoints);
  ringOutlineGlyph->SetScaleFactor(this->WidgetScale);
  ringOutlineGlyph->ScalingOff();
  ringOutlineGlyph->ExtractEigenvaluesOff();
  ringOutlineGlyph->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  HandleInfoList handleInfoList = this->GetHandleInfoList();
  for (HandleInfo handleInfo : handleInfoList)
    {
    vtkPolyData* outputPoints = nullptr;
    vtkPolyData* outlinePoints = nullptr;
    switch (handleInfo.GlyphType)
      {
      case GlyphArrow:
        outputPoints = arrowPoints;
        outlinePoints = arrowOutlinePoints;
        break;
      case GlyphCircle:
        outputPoints = circlePoints;
        outlinePoints = circleOutlinePoints;
        break;
      case GlyphRing:
        outputPoints = ringPoints;
        outlinePoints = ringOutlinePoints;
        break;
      default:
        break;
      }

    if (!outputPoints)
      {
      continue;
      }

    vtkPolyData* handlePolyData = this->GetHandlePolydata(handleInfo.ComponentType);
    if (!handlePolyData)
      {
      continue;
      }

    // Copy point and array values at handleInfo.ComponentIndex
    if (!outputPoints->GetPoints())
      {
      outputPoints->SetPoints(vtkNew<vtkPoints>());
      }
    if (!outlinePoints->GetPoints())
      {
      outlinePoints->SetPoints(vtkNew<vtkPoints>());
      }

    double point[3] = { 0.0, 0.0, 0.0 };
    handlePolyData->GetPoints()->GetPoint(handleInfo.Index, point);
    vtkMath::MultiplyScalar(point, this->WidgetScale);

    outputPoints->GetPoints()->InsertNextPoint(point);
    outlinePoints->GetPoints()->InsertNextPoint(point);

    vtkPointData* pointData = handlePolyData->GetPointData();
    vtkPointData* outputPointData = outputPoints->GetPointData();
    vtkPointData* outlinePointData = outlinePoints->GetPointData();
    for (int arrayIndex = 0; arrayIndex < pointData->GetNumberOfArrays(); ++arrayIndex)
      {
      vtkDataArray* array = pointData->GetArray(arrayIndex);

      vtkSmartPointer<vtkDataArray> outputArray = outputPointData->GetArray(array->GetName());
      if (!outputArray)
        {
        outputArray = vtkSmartPointer<vtkDataArray>::Take(array->NewInstance());
        outputArray->SetName(array->GetName());
        outputArray->SetNumberOfComponents(array->GetNumberOfComponents());
        outputArray->SetNumberOfTuples(0);
        outputPointData->AddArray(outputArray);
        }
      outputArray->InsertNextTuple(array->GetTuple(handleInfo.Index));

      vtkSmartPointer<vtkDataArray> outlineArray = outlinePointData->GetArray(array->GetName());
      if (!outlineArray)
        {
        outlineArray = vtkSmartPointer<vtkDataArray>::Take(array->NewInstance());
        outlineArray->SetName(array->GetName());
        outlineArray->SetNumberOfComponents(array->GetNumberOfComponents());
        outlineArray->SetNumberOfTuples(0);
        outlinePointData->AddArray(outlineArray);
        }
      if (strcmp(array->GetName(), "colorIndex") == 0)
        {
        outlineArray->InsertNextTuple1(array->GetTuple(handleInfo.Index)[0] + 1);
        }
      else
        {
        outlineArray->InsertNextTuple(array->GetTuple(handleInfo.Index));
        }

      }
    }

  arrowPoints->GetPointData()->SetActiveScalars("colorIndex");
  arrowOutlinePoints->GetPointData()->SetActiveScalars("colorIndex");
  circlePoints->GetPointData()->SetActiveScalars("colorIndex");
  circleOutlinePoints->GetPointData()->SetActiveScalars("colorIndex");
  ringPoints->GetPointData()->SetActiveScalars("colorIndex");
  ringOutlinePoints->GetPointData()->SetActiveScalars("colorIndex");

  this->Pipeline->Append->AddInputConnection(arrowGlyph->GetOutputPort());
  this->Pipeline->Append->AddInputConnection(arrowOutlineGlyph->GetOutputPort());
  this->Pipeline->Append->AddInputConnection(circleGlyph->GetOutputPort());
  this->Pipeline->Append->AddInputConnection(circleOutlineGlyph->GetOutputPort());
  this->Pipeline->Append->AddInputConnection(ringGlyph->GetOutputPort());
  this->Pipeline->Append->AddInputConnection(ringOutlineGlyph->GetOutputPort());
}

//----------------------------------------------------------------------
vtkPointPlacer* vtkMRMLInteractionWidgetRepresentation::GetPointPlacer()
{
  return this->PointPlacer;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetActors(vtkPropCollection* pc)
{
  vtkProp* actor = this->GetInteractionActor();
  if (actor)
    {
    actor->GetActors(pc);
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::ReleaseGraphicsResources(vtkWindow* window)
{
  vtkProp* actor = this->GetInteractionActor();
  if (actor)
    {
    actor->ReleaseGraphicsResources(window);
    }
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::RenderOverlay(vtkViewport* viewport)
{
  int count = 0;
  vtkProp* actor = this->GetInteractionActor();
  if (this->Pipeline && actor->GetVisibility())
    {
    count += actor->RenderOverlay(viewport);
    }
  return count;
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::RenderOpaqueGeometry(vtkViewport* viewport)
{
  if (!this->Pipeline)
    {
    this->SetupInteractionPipeline();
    }

  int count = 0;
  vtkProp* actor = this->GetInteractionActor();
  if (actor && actor->GetVisibility())
    {
    this->UpdateHandleToWorldTransform();
    this->UpdateSlicePlaneFromSliceNode();
    this->UpdateHandleColors();
    this->UpdateViewScaleFactor();
    this->UpdateHandleOrientation();
    this->UpdateHandleSize();
    this->UpdateHandlePolyData();
    count += actor->RenderOpaqueGeometry(viewport);
    this->Pipeline->HandleToWorldTransformFilter->Update();
    if (this->Pipeline->HandleToWorldTransformFilter->GetOutput()->GetNumberOfPoints() > 0)
      {
      double bounds[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
      this->Pipeline->HandleToWorldTransformFilter->GetOutput()->GetBounds(bounds);
      }
    this->Pipeline->WorldToSliceTransformFilter->Update();
    if (this->Pipeline->WorldToSliceTransformFilter->GetOutput()->GetNumberOfPoints() > 0)
      {
      double bounds[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
      this->Pipeline->WorldToSliceTransformFilter->GetOutput()->GetBounds(bounds);
      }
    }
  return count;
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::RenderTranslucentPolygonalGeometry(vtkViewport* viewport)
{
  int count = 0;
  vtkProp* actor = this->GetInteractionActor();
  if (actor && actor->GetVisibility())
    {
    actor->SetPropertyKeys(this->GetPropertyKeys());
    count += actor->RenderTranslucentPolygonalGeometry(viewport);
    }
  return count;
}

//----------------------------------------------------------------------
vtkTypeBool vtkMRMLInteractionWidgetRepresentation::HasTranslucentPolygonalGeometry()
{
  vtkProp* actor = this->GetInteractionActor();
  if (actor && actor->GetVisibility() &&
    actor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  return false;
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::InteractionPipeline::InteractionPipeline()
{
  /// Rotation pipeline
  this->RotationHandlePoints = vtkSmartPointer<vtkPolyData>::New();

  vtkNew<vtkEllipseArcSource> outerArcSource;
  outerArcSource->SetMajorRadiusVector(-INTERACTION_HANDLE_ROTATION_ARC_OUTER_RADIUS, 0.0, 0.0);
  outerArcSource->SetResolution(INTERACTION_HANDLE_ROTATION_ARC_RESOLUTION);
  outerArcSource->SetCenter(0, 0, 0);
  outerArcSource->SetNormal(0, 0, 1);
  outerArcSource->SetRatio(1.0);
  outerArcSource->SetStartAngle(180 - INTERACTION_HANDLE_ROTATION_ARC_DEGREES / 2.0);
  outerArcSource->SetSegmentAngle(INTERACTION_HANDLE_ROTATION_ARC_DEGREES);
  outerArcSource->Update();

  vtkNew<vtkEllipseArcSource> innerArcSource;
  innerArcSource->SetMajorRadiusVector(-INTERACTION_HANDLE_ROTATION_ARC_INNER_RADIUS, 0.0, 0.0);
  innerArcSource->SetResolution(INTERACTION_HANDLE_ROTATION_ARC_RESOLUTION);
  innerArcSource->SetCenter(0, 0, 0);
  innerArcSource->SetNormal(0, 0, 1);
  innerArcSource->SetRatio(1.0);
  innerArcSource->SetStartAngle(180 - INTERACTION_HANDLE_ROTATION_ARC_DEGREES / 2.0);
  innerArcSource->SetSegmentAngle(INTERACTION_HANDLE_ROTATION_ARC_DEGREES);
  innerArcSource->Update();

  vtkNew<vtkPoints> rotationPts;

  this->RingOutlinePolyData = vtkSmartPointer <vtkPolyData>::New();
  this->RingOutlinePolyData->SetPoints(rotationPts);
  this->RingOutlinePolyData->SetLines(vtkNew<vtkCellArray>());

  this->RingPolyData = vtkSmartPointer <vtkPolyData>::New();
  this->RingPolyData->SetPoints(rotationPts);
  this->RingPolyData->SetPolys(vtkNew<vtkCellArray>());

  if (INTERACTION_HANDLE_ROTATION_ARC_DEGREES < 360.0)
    {
    vtkNew<vtkIdList> rotationPoly;
    vtkNew<vtkIdList> rotationLine;

    for (int i = 0; i < outerArcSource->GetOutput()->GetNumberOfPoints(); ++i)
      {
      double point[3];
      outerArcSource->GetOutput()->GetPoint(i, point);
      vtkIdType id = rotationPts->InsertNextPoint(point);
      rotationPoly->InsertNextId(id);
      rotationLine->InsertNextId(id);
      }
    for (int i = innerArcSource->GetOutput()->GetNumberOfPoints() - 1; i >= 0; --i)
      {
      double point[3];
      innerArcSource->GetOutput()->GetPoint(i, point);
      vtkIdType id = rotationPts->InsertNextPoint(point);
      rotationPoly->InsertNextId(id);
      rotationLine->InsertNextId(id);
      }
      rotationLine->InsertNextId(0);
      this->ArrowOutlinePolyData->InsertNextCell(VTK_POLY_LINE, rotationLine);
      this->ArrowPolyData->InsertNextCell(VTK_POLYGON, rotationPoly);
    }
  else
    {
    vtkNew<vtkCellArray> rotationTriangles;
    vtkNew<vtkIdList> outerLine;
    vtkNew<vtkIdList> innerLine;

    vtkIdType previousInnerPoint = -1;
    vtkIdType previousOuterPoint = -1;
    for (int index = 0; index < outerArcSource->GetOutput()->GetNumberOfPoints(); ++index)
      {
      double outerLinePoint[3] = { 0.0, 0.0, 0.0 };
      outerArcSource->GetOutput()->GetPoint(index, outerLinePoint);
      vtkIdType outerPointId = rotationPts->InsertNextPoint(outerLinePoint);
      outerLine->InsertNextId(outerPointId);

      double innerLinePoint[3] = { 0.0, 0.0, 0.0 };
      innerArcSource->GetOutput()->GetPoint(index, innerLinePoint);
      vtkIdType innerPointId = rotationPts->InsertNextPoint(innerLinePoint);
      innerLine->InsertNextId(innerPointId);

      if (previousInnerPoint >= 0 && previousOuterPoint >= 0)
        {
        vtkNew<vtkIdList> rotationTriangleA;
        rotationTriangleA->InsertNextId(previousInnerPoint);
        rotationTriangleA->InsertNextId(previousOuterPoint);
        rotationTriangleA->InsertNextId(outerPointId);
        rotationTriangles->InsertNextCell(rotationTriangleA);

        vtkNew<vtkIdList> rotationTriangleB;
        rotationTriangleB->InsertNextId(previousInnerPoint);
        rotationTriangleB->InsertNextId(outerPointId);
        rotationTriangleB->InsertNextId(innerPointId);
        rotationTriangles->InsertNextCell(rotationTriangleB);
        }
      previousInnerPoint = innerPointId;
      previousOuterPoint = outerPointId;
      }

    if (previousInnerPoint > 0 && previousOuterPoint > 0)
      {
      vtkNew<vtkIdList> rotationTriangleA;
      rotationTriangleA->InsertNextId(previousInnerPoint);
      rotationTriangleA->InsertNextId(previousOuterPoint);
      rotationTriangleA->InsertNextId(0);
      rotationTriangles->InsertNextCell(rotationTriangleA);

      vtkNew<vtkIdList> rotationTriangleB;
      rotationTriangleB->InsertNextId(previousInnerPoint);
      rotationTriangleB->InsertNextId(0);
      rotationTriangleB->InsertNextId(1);
      rotationTriangles->InsertNextCell(rotationTriangleB);
      }

    this->RingOutlinePolyData->InsertNextCell(VTK_POLY_LINE, outerLine);
    this->RingOutlinePolyData->InsertNextCell(VTK_POLY_LINE, innerLine);
    this->RingPolyData->SetPolys(rotationTriangles);
    }

  vtkNew<vtkTriangleFilter> triangleFilter;
  triangleFilter->SetInputData(this->RingPolyData);

  vtkNew<vtkPoints> translationHandlePoints;
  vtkNew<vtkIdList> translationHandlePoly;
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint( 0.00,  0.00, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(
    -INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(
    -INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(
    -INTERACTION_TRANSLATION_HANDLE_LENGTH, INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(
    -INTERACTION_TRANSLATION_HANDLE_LENGTH, -INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(
    -INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, -INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(
    -INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, -INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, 0.00));

  vtkNew<vtkIdList> translationHandleLine;
  translationHandleLine->DeepCopy(translationHandlePoly);
  translationHandleLine->InsertNextId(0);

  this->ArrowPolyData = vtkSmartPointer <vtkPolyData>::New();
  this->ArrowPolyData->SetPoints(translationHandlePoints);
  this->ArrowPolyData->SetPolys(vtkNew<vtkCellArray>());
  this->ArrowPolyData->InsertNextCell(VTK_POLYGON, translationHandlePoly);

  this->ArrowOutlinePolyData = vtkSmartPointer <vtkPolyData>::New();
  this->ArrowOutlinePolyData->SetPoints(translationHandlePoints);
  this->ArrowOutlinePolyData->SetLines(vtkNew<vtkCellArray>());
  this->ArrowOutlinePolyData->InsertNextCell(VTK_POLY_LINE, translationHandleLine);

  /// Translation pipeline
  this->TranslationHandlePoints = vtkSmartPointer<vtkPolyData>::New();

  /// Scale pipeline
  vtkNew<vtkEllipseArcSource> scaleArcSource;
  scaleArcSource->SetMajorRadiusVector(INTERACTION_HANDLE_SCALE_RADIUS, 0.0, 0.0);
  scaleArcSource->SetResolution(100);
  scaleArcSource->SetCenter(0.0, 0.0, 0.0);
  scaleArcSource->SetNormal(0, 0, 1);
  scaleArcSource->SetRatio(1.0);
  scaleArcSource->SetStartAngle(0);
  scaleArcSource->SetSegmentAngle(360);
  scaleArcSource->Update();

  vtkNew<vtkPoints> scalePoints;
  vtkNew<vtkIdList> scalePoly;
  vtkNew<vtkIdList> scaleLine;
  for (int i = 0; i < scaleArcSource->GetOutput()->GetNumberOfPoints(); ++i)
    {
    double point[3];
    scaleArcSource->GetOutput()->GetPoint(i, point);
    vtkIdType id = scalePoints->InsertNextPoint(point);
    scalePoly->InsertNextId(id);
    scaleLine->InsertNextId(id);
    }
  scaleLine->InsertNextId(0);

  this->CirclePolyData = vtkSmartPointer<vtkPolyData>::New();
  this->CirclePolyData->SetPoints(scalePoints);
  this->CirclePolyData->SetPolys(vtkNew<vtkCellArray>());
  this->CirclePolyData->InsertNextCell(VTK_POLYGON, scalePoly);

  this->CircleOutlinePolyData = vtkSmartPointer<vtkPolyData>::New();
  this->CircleOutlinePolyData->SetPoints(scalePoints);
  this->CircleOutlinePolyData->SetLines(vtkNew<vtkCellArray>());
  this->CircleOutlinePolyData->InsertNextCell(VTK_POLY_LINE, scaleLine);

  this->ScaleHandlePoints = vtkSmartPointer<vtkPolyData>::New();

  this->Append = vtkSmartPointer<vtkAppendPolyData>::New();

  this->HandleToWorldTransform = vtkSmartPointer<vtkTransform>::New();
  this->HandleToWorldTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->HandleToWorldTransformFilter->SetInputConnection(this->Append->GetOutputPort());
  this->HandleToWorldTransformFilter->SetTransform(this->HandleToWorldTransform);

  this->ColorTable = vtkSmartPointer<vtkLookupTable>::New();

  this->Mapper3D = vtkSmartPointer<vtkPolyDataMapper>::New();
  this->Mapper3D->SetInputConnection(this->HandleToWorldTransformFilter->GetOutputPort());
  this->Mapper3D->SetColorModeToMapScalars();
  this->Mapper3D->ColorByArrayComponent("colorIndex", 0);
  this->Mapper3D->SetLookupTable(this->ColorTable);
  this->Mapper3D->ScalarVisibilityOn();
  this->Mapper3D->UseLookupTableScalarRangeOn();

  this->Property3D = vtkSmartPointer<vtkProperty>::New();
  this->Property3D->SetPointSize(0.0);
  this->Property3D->SetLineWidth(2.0);
  this->Property3D->SetDiffuse(0.0);
  this->Property3D->SetAmbient(1.0);
  this->Property3D->SetMetallic(0.0);
  this->Property3D->SetSpecular(0.0);
  this->Property3D->SetEdgeVisibility(true);

  this->Actor3D = vtkSmartPointer<vtkActor>::New();
  this->Actor3D->SetProperty(this->Property3D);
  this->Actor3D->SetMapper(this->Mapper3D);

  this->WorldToSliceTransform = vtkSmartPointer<vtkTransform>::New();

  this->WorldToSliceTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->WorldToSliceTransformFilter->SetTransform(this->WorldToSliceTransform);
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::InteractionPipeline::~InteractionPipeline() = default;

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::InitializePipeline()
{
  this->CreateRotationHandles();
  this->CreateTranslationHandles();
  this->CreateScaleHandles();
  this->UpdateHandleColors();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CreateRotationHandles()
{
  vtkNew<vtkPoints> points;

  double xRotationHandle[3] = { 0, 0, 0 }; // X-axis
  points->InsertNextPoint(xRotationHandle);
  double yRotationHandle[3] = { 0, 0, 0 }; // Y-axis
  vtkMath::Normalize(yRotationHandle);
  points->InsertNextPoint(yRotationHandle);
  double zRotationHandle[3] = { 0, 0, 0 }; // Z-axis
  vtkMath::Normalize(zRotationHandle);
  points->InsertNextPoint(zRotationHandle);
  this->Pipeline->RotationHandlePoints->SetPoints(points);
  double viewPlaneRotationHandle[3] = { 0, 0, 0 }; // View
  this->Pipeline->RotationHandlePoints->GetPoints()->InsertNextPoint(viewPlaneRotationHandle);

  vtkNew<vtkDoubleArray> orientationArray;
  orientationArray->SetName("orientation");
  orientationArray->SetNumberOfComponents(9);
  vtkNew<vtkTransform> xRotationOrientation;
  xRotationOrientation->RotateX(90);
  xRotationOrientation->RotateY(90);
  xRotationOrientation->RotateZ(45);
  vtkMatrix4x4* xRotationMatrix = xRotationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(xRotationMatrix->GetElement(0, 0), xRotationMatrix->GetElement(1, 0), xRotationMatrix->GetElement(2, 0),
                                     xRotationMatrix->GetElement(0, 1), xRotationMatrix->GetElement(1, 1), xRotationMatrix->GetElement(2, 1),
                                     xRotationMatrix->GetElement(0, 2), xRotationMatrix->GetElement(1, 2), xRotationMatrix->GetElement(2, 2));
  vtkNew<vtkTransform> yRotationOrientation;
  yRotationOrientation->RotateX(90);
  yRotationOrientation->RotateZ(45);
  vtkMatrix4x4* yRotationMatrix = yRotationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(yRotationMatrix->GetElement(0, 0), yRotationMatrix->GetElement(1, 0), yRotationMatrix->GetElement(2, 0),
                                     yRotationMatrix->GetElement(0, 1), yRotationMatrix->GetElement(1, 1), yRotationMatrix->GetElement(2, 1),
                                     yRotationMatrix->GetElement(0, 2), yRotationMatrix->GetElement(1, 2), yRotationMatrix->GetElement(2, 2));
  vtkNew<vtkTransform> zRotationOrientation;
  zRotationOrientation->RotateZ(45);
  vtkMatrix4x4* zRotationMatrix = zRotationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(zRotationMatrix->GetElement(0, 0), zRotationMatrix->GetElement(1, 0), zRotationMatrix->GetElement(2, 0),
                                     zRotationMatrix->GetElement(0, 1), zRotationMatrix->GetElement(1, 1), zRotationMatrix->GetElement(2, 1),
                                     zRotationMatrix->GetElement(0, 2), zRotationMatrix->GetElement(1, 2), zRotationMatrix->GetElement(2, 2));

  orientationArray->InsertNextTuple9(1,0,0,0,1,0,0,0,1); // View
  this->Pipeline->RotationHandlePoints->GetPointData()->AddArray(orientationArray);

  vtkNew<vtkIdTypeArray> visibilityArray;
  visibilityArray->SetName("visibility");
  visibilityArray->SetNumberOfComponents(1);
  visibilityArray->SetNumberOfValues(this->Pipeline->RotationHandlePoints->GetNumberOfPoints());
  visibilityArray->Fill(1);
  this->Pipeline->RotationHandlePoints->GetPointData()->AddArray(visibilityArray);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CreateTranslationHandles()
{
  vtkNew<vtkPoints> points;
  points->InsertNextPoint(1.0, 0.0, 0.0); // X-axis
  points->InsertNextPoint(0.0, 1.0, 0.0); // Y-axis
  points->InsertNextPoint(0.0, 0.0, 1.0); // Z-axis
  points->InsertNextPoint(0.0, 0.0, 0.0); // Free translation
  this->Pipeline->TranslationHandlePoints->SetPoints(points);

  vtkNew<vtkDoubleArray> orientationArray;
  orientationArray->SetName("orientation");
  orientationArray->SetNumberOfComponents(9);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 1.0,
                                     0.0, 0.0, 0.0,
                                     0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0); // Free translation
  this->Pipeline->TranslationHandlePoints->GetPointData()->AddArray(orientationArray);

  vtkNew<vtkIdTypeArray> visibilityArray;
  visibilityArray->SetName("visibility");
  visibilityArray->SetNumberOfComponents(1);
  visibilityArray->SetNumberOfValues(this->Pipeline->TranslationHandlePoints->GetNumberOfPoints());
  visibilityArray->Fill(1);
  this->Pipeline->TranslationHandlePoints->GetPointData()->AddArray(visibilityArray);
}

//----------------------------------------------------------------------
vtkProp* vtkMRMLInteractionWidgetRepresentation::GetInteractionActor()
{
  if (!this->Pipeline)
    {
    return nullptr;
    }
  return this->Pipeline->Actor3D;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateHandleOrientation()
{
  this->UpdateTranslationHandleOrientation();
  this->UpdateScaleHandleOrientation();
  this->UpdateRotationHandleOrientation();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateTranslationHandleOrientation()
{
  vtkPolyData* handlePolyData = this->GetHandlePolydata(InteractionTranslationHandle);
  if (!handlePolyData)
    {
    return;
    }

  vtkDoubleArray* orientationArray = vtkDoubleArray::SafeDownCast(handlePolyData->GetPointData()->GetAbstractArray("orientation"));
  if (!orientationArray)
    {
    return;
    }

  double viewDirection_World[3] = { 0.0, 0.0, 0.0 };
  double viewDirection_Handle[3] = { 0.0, 0.0, 0.0 };

  if (this->GetSliceNode())
    {
    this->SlicePlane->GetNormal(viewDirection_World);
    }
  else
    {
    this->Renderer->GetActiveCamera()->GetDirectionOfProjection(viewDirection_World);
    }
  vtkTransform::SafeDownCast(this->Pipeline->HandleToWorldTransform->GetInverse())->TransformVector(viewDirection_World, viewDirection_Handle);

  for (int i = 0; i < 3; ++i)
    {
    double xAxis[3] = { 0.0, 0.0, 0.0 };
    double yAxis[3] = { 0.0, 0.0, 0.0 };
    double zAxis[3] = { 0.0, 0.0, 0.0 };
    xAxis[i] = 1.0;

    vtkMath::Cross(viewDirection_Handle, xAxis, yAxis);
    vtkMath::Normalize(yAxis);

    vtkMath::Cross(xAxis, yAxis, zAxis);
    vtkMath::Normalize(zAxis);

    orientationArray->SetTuple9(i, xAxis[0], xAxis[1], xAxis[2],
                                   yAxis[0], yAxis[1], yAxis[2],
                                   zAxis[0], zAxis[1], zAxis[2]);
    }

  double viewUp_World[3] = { 0.0, 1.0, 0.0 };
  double viewUp_Handle[3] = { 0.0, 0.0, 0.0 };
  vtkTransform::SafeDownCast(this->Pipeline->HandleToWorldTransform->GetInverse())->TransformVector(viewDirection_World, viewDirection_Handle);
  vtkTransform::SafeDownCast(this->Pipeline->HandleToWorldTransform->GetInverse())->TransformVector(viewUp_World, viewUp_Handle);
  if (this->GetSliceNode())
    {
    this->SlicePlane->GetNormal(viewDirection_World);
    double viewup[4] = { 0,1,0,0 };
    double viewup2[4] = { 0,1,0,0 };
    this->GetSliceNode()->GetXYToRAS()->MultiplyPoint(viewup, viewup2);
    viewUp_World[0] = viewup2[0];
    viewUp_World[1] = viewup2[1];
    viewUp_World[2] = viewup2[2];
    }
  else
    {
    this->Renderer->GetActiveCamera()->GetDirectionOfProjection(viewDirection_World);
    this->Renderer->GetActiveCamera()->GetViewUp(viewUp_World);
    }
  double xAxis[3] = { 0.0, 0.0, 0.0 };
  double yAxis[3] = { 0.0, 0.0, 0.0 };
  double zAxis[3] = { 0.0, 0.0, 0.0 };
  zAxis[0] = viewDirection_Handle[0];
  zAxis[1] = viewDirection_Handle[1];
  zAxis[2] = viewDirection_Handle[2];
  vtkMath::Normalize(zAxis);

  vtkMath::Cross(viewUp_Handle, viewDirection_Handle, xAxis);
  vtkMath::Normalize(xAxis);

  vtkMath::Cross(viewDirection_Handle, xAxis, yAxis);
  vtkMath::Normalize(yAxis);

  orientationArray->SetTuple9(3, xAxis[0], xAxis[1], xAxis[2],
    yAxis[0], yAxis[1], yAxis[2],
    zAxis[0], zAxis[1], zAxis[2]);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateScaleHandleOrientation()
{
  vtkPolyData* handlePolyData = this->GetHandlePolydata(InteractionScaleHandle);
  if (!handlePolyData)
    {
    return;
    }

  vtkDoubleArray* orientationArray = vtkDoubleArray::SafeDownCast(handlePolyData->GetPointData()->GetAbstractArray("orientation"));
  if (!orientationArray)
    {
    return;
    }

  double viewDirection_World[3] = { 0.0, 0.0, 0.0 };
  double viewDirection_Handle[3] = { 0.0, 0.0, 0.0 };

  double viewUp_World[3] = { 0.0, 1.0, 0.0 };
  double viewUp_Handle[3] = { 0.0, 0.0, 0.0 };

  if (this->GetSliceNode())
    {
    this->SlicePlane->GetNormal(viewDirection_World);
    double viewup[4] = { 0,1,0,0 };
    double viewup2[4] = { 0,1,0,0 };
    this->GetSliceNode()->GetXYToRAS()->MultiplyPoint(viewup, viewup2);
    viewUp_World[0] = viewup2[0];
    viewUp_World[1] = viewup2[1];
    viewUp_World[2] = viewup2[2];
    }
  else
    {
    this->Renderer->GetActiveCamera()->GetDirectionOfProjection(viewDirection_World);
    this->Renderer->GetActiveCamera()->GetViewUp(viewUp_World);
    }
  vtkTransform::SafeDownCast(this->Pipeline->HandleToWorldTransform->GetInverse())->TransformVector(viewDirection_World, viewDirection_Handle);
  vtkTransform::SafeDownCast(this->Pipeline->HandleToWorldTransform->GetInverse())->TransformVector(viewUp_World, viewUp_Handle);

  for (int i = 0; i < orientationArray->GetNumberOfTuples(); ++i)
    {
    double xAxis[3] = { 1.0, 0.0, 0.0 };
    double yAxis[3] = { 0.0, 1.0, 0.0 };
    double zAxis[3] = { 0.0, 0.0, 1.0 };
    zAxis[0] = viewDirection_Handle[0];
    zAxis[1] = viewDirection_Handle[1];
    zAxis[2] = viewDirection_Handle[2];
    vtkMath::Normalize(zAxis);

    vtkMath::Cross(viewUp_Handle, viewDirection_Handle, xAxis);
    vtkMath::Normalize(xAxis);

    vtkMath::Cross(viewDirection_Handle, xAxis, yAxis);
    vtkMath::Normalize(yAxis);

    orientationArray->SetTuple9(i, xAxis[0], xAxis[1], xAxis[2],
                                   yAxis[0], yAxis[1], yAxis[2],
                                   zAxis[0], zAxis[1], zAxis[2]);
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateRotationHandleOrientation()
{
  vtkPolyData* handlePolyData = this->GetHandlePolydata(InteractionRotationHandle);
  if (!handlePolyData)
    {
    return;
    }

  vtkDoubleArray* orientationArray = vtkDoubleArray::SafeDownCast(handlePolyData->GetPointData()->GetAbstractArray("orientation"));
  if (!orientationArray)
    {
    return;
    }

  double viewDirection_World[3] = { 0.0, 0.0, 0.0 };
  double viewDirection_Handle[3] = { 0.0, 0.0, 0.0 };

  double viewUp_World[3] = { 0.0, 1.0, 0.0 };
  double viewUp_Handle[3] = { 0.0, 0.0, 0.0 };

  if (this->GetSliceNode())
    {
    this->SlicePlane->GetNormal(viewDirection_World);
    double viewup[4] = { 0,1,0,0 };
    double viewup2[4] = { 0,1,0,0 };
    this->GetSliceNode()->GetXYToRAS()->MultiplyPoint(viewup, viewup2);
    viewUp_World[0] = viewup2[0];
    viewUp_World[1] = viewup2[1];
    viewUp_World[2] = viewup2[2];
    }
  else
    {
    this->Renderer->GetActiveCamera()->GetDirectionOfProjection(viewDirection_World);
    this->Renderer->GetActiveCamera()->GetViewUp(viewUp_World);
    }
  vtkTransform::SafeDownCast(this->Pipeline->HandleToWorldTransform->GetInverse())->TransformVector(viewDirection_World, viewDirection_Handle);
  vtkTransform::SafeDownCast(this->Pipeline->HandleToWorldTransform->GetInverse())->TransformVector(viewUp_World, viewUp_Handle);

  double xAxis[3] = { 1.0, 0.0, 0.0 };
  double yAxis[3] = { 0.0, 1.0, 0.0 };
  double zAxis[3] = { 0.0, 0.0, 1.0 };
  zAxis[0] = viewDirection_Handle[0];
  zAxis[1] = viewDirection_Handle[1];
  zAxis[2] = viewDirection_Handle[2];
  vtkMath::Normalize(zAxis);

  vtkMath::Cross(viewUp_Handle, viewDirection_Handle, xAxis);
  vtkMath::Normalize(xAxis);

  vtkMath::Cross(viewDirection_Handle, xAxis, yAxis);
  vtkMath::Normalize(yAxis);

  orientationArray->SetTuple9(3, xAxis[0], xAxis[1], xAxis[2],
                                 yAxis[0], yAxis[1], yAxis[2],
                                 zAxis[0], zAxis[1], zAxis[2]);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CreateScaleHandles()
{
  vtkNew<vtkPoints> points;
  points->InsertNextPoint(1.4, 0.0, 0.0); // X-axis
  points->InsertNextPoint(0.0, 1.4, 0.0); // Y-axis
  points->InsertNextPoint(0.0, 0.0, 1.4); // Z-axis
  this->Pipeline->ScaleHandlePoints->SetPoints(points);

  vtkNew<vtkDoubleArray> orientationArray;
  orientationArray->SetName("orientation");
  orientationArray->SetNumberOfComponents(9);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 1.0,
                                     0.0, 0.0, 0.0,
                                     0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(0.0, 0.0, 0.0,
                                     0.0, 0.0, 0.0,
                                     0.0, 0.0, 0.0); // Free translation
  this->Pipeline->ScaleHandlePoints->GetPointData()->AddArray(orientationArray);

  vtkNew<vtkIdTypeArray> visibilityArray;
  visibilityArray->SetName("visibility");
  visibilityArray->SetNumberOfComponents(1);
  visibilityArray->SetNumberOfValues(this->Pipeline->ScaleHandlePoints->GetNumberOfPoints());
  visibilityArray->Fill(1);
  this->Pipeline->ScaleHandlePoints->GetPointData()->AddArray(visibilityArray);
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::GetNumberOfHandles()
{
  int numberOfHandles = 0;
  for (int type = InteractionNone+1; type < Interaction_Last; ++type)
    {
    numberOfHandles += this->GetNumberOfHandles(type);
    }
  return numberOfHandles;
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::GetNumberOfHandles(int type)
{
  vtkPolyData* handlePolyData = this->GetHandlePolydata(type);
  if (!handlePolyData)
    {
    vtkErrorMacro( << "GetNumberOfHandles: Invalid handle type: " << type);
    return 0;
    }
  return handlePolyData->GetNumberOfPoints();
}

//----------------------------------------------------------------------
vtkPolyData* vtkMRMLInteractionWidgetRepresentation::GetHandlePolydata(int type)
{
  if (type == InteractionRotationHandle)
    {
    return this->Pipeline->RotationHandlePoints;
    }
  else if (type == InteractionTranslationHandle)
    {
    return this->Pipeline->TranslationHandlePoints;
    }
  else if (type == InteractionScaleHandle)
    {
    return this->Pipeline->ScaleHandlePoints;
    }
  return nullptr;
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::UpdateHandleColors(int type, int colorIndex)
{
  vtkPolyData* handlePolyData = this->GetHandlePolydata(type);
  if (!handlePolyData)
    {
    return colorIndex;
    }

  vtkPointData* pointData = handlePolyData->GetPointData();
  if (!pointData)
    {
    return colorIndex;
    }

  vtkSmartPointer<vtkFloatArray> colorArray = vtkFloatArray::SafeDownCast(
    pointData->GetAbstractArray("colorIndex"));
  if (!colorArray)
    {
    colorArray = vtkSmartPointer<vtkFloatArray>::New();
    colorArray->SetName("colorIndex");
    colorArray->SetNumberOfComponents(1);
    pointData->AddArray(colorArray);
    pointData->SetActiveScalars("colorIndex");
    }
  colorArray->Initialize();
  colorArray->SetNumberOfTuples(handlePolyData->GetNumberOfPoints()*2);

  double color[4] = { 0.0, 0.0, 0.0, 0.0 };
  for (int i = 0; i < handlePolyData->GetNumberOfPoints(); ++i)
    {
    this->GetHandleColor(type, i, color);
    this->Pipeline->ColorTable->SetTableValue(colorIndex, color);
    colorArray->SetTuple1(i, colorIndex);
    ++colorIndex;

    double grey = 0.3;
    bool selected = this->GetActiveComponentType() == type && this->GetActiveComponentIndex() == i;
    if (selected)
      {
      grey = 0.0;
      }
    this->Pipeline->ColorTable->SetTableValue(colorIndex, grey, grey, grey, color[3]);
    ++colorIndex;
    }

  return colorIndex;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateHandleColors()
{
  int numberOfHandles = this->GetNumberOfHandles();
  int numberOfColors = numberOfHandles * 2; // alternate fill and outline colors
  this->Pipeline->ColorTable->SetNumberOfTableValues(numberOfColors);
  this->Pipeline->ColorTable->SetTableRange(0, double(numberOfColors) - 1);

  int colorIndex = 0;
  colorIndex = this->UpdateHandleColors(InteractionRotationHandle, colorIndex);
  colorIndex = this->UpdateHandleColors(InteractionTranslationHandle, colorIndex);
  colorIndex = this->UpdateHandleColors(InteractionScaleHandle, colorIndex);

  this->Pipeline->ColorTable->Build();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetHandleColor(int type, int index, double color[4])
{
  if (!color)
    {
    return;
    }

  double red[3]    = { 0.80, 0.35, 0.35 };
  double redSelected[3] = { 0.70, 0.07, 0.07 };

  double green[4] = { 0.35, 0.80, 0.35 };
  double greenSelected[4] = { 0.07, 0.70, 0.07 };

  double blue[4]   = { 0.35, 0.35, 0.8 };
  double blueSelected[4] = { 0.07, 0.07, 0.70 };

  double orange[4] = { 0.80, 0.65, 0.35 };
  double orangeSelected[4] = { 0.70, 0.50, 0.07 };

  double white[4]  = { 0.80, 0.80, 0.80 };
  double whiteSelected[4] = { 1.00, 1.00, 1.00 };

  bool selected = this->GetActiveComponentType() == type && this->GetActiveComponentIndex() == index;

  double* currentColor = white;
  switch (index)
    {
    case 0:
      currentColor = selected ? redSelected : red;
      break;
    case 1:
      currentColor = selected ? greenSelected : green;
      break;
    case 2:
      currentColor = selected ? blueSelected : blue;
      break;
    case 3:
      currentColor = selected ? whiteSelected: white;
      break;
    default:
      currentColor = selected ? whiteSelected : white;
      break;
    }

  double opacity = this->GetHandleOpacity(type, index);
  if (selected)
    {
    opacity = 1.0;
    }

  for (int i = 0; i < 3; ++i)
    {
    color[i] = currentColor[i];
    }
  color[3] = opacity;
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::GetHandleGlyphType(int type, int index)
{
  if (type == InteractionRotationHandle)
    {
    return GlyphRing;
    }
  else if (type == InteractionTranslationHandle && index < 3)
    {
    return GlyphArrow;
    }
  return GlyphCircle;
}

//----------------------------------------------------------------------
bool vtkMRMLInteractionWidgetRepresentation::GetHandleVisibility(int type, int index)
{
  vtkPolyData* handlePolyData = this->GetHandlePolydata(type);
  vtkIdTypeArray* visibilityArray = nullptr;
  if (handlePolyData)
    {
    visibilityArray = vtkIdTypeArray::SafeDownCast(handlePolyData->GetPointData()->GetArray("visibility"));
    }
  if (visibilityArray && index < visibilityArray->GetNumberOfValues())
    {
     return visibilityArray->GetValue(index) != 0;
    }
  return true;
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetHandleOpacity(int type, int index)
{
  // Determine if the handle should be displayed
  bool handleVisible = this->GetHandleVisibility(type, index);
  if (!handleVisible)
    {
    return 0.0;
    }

  double opacity = 1.0;

  double axis_World[3] = { 0.0, 0.0, 0.0 };
  this->GetInteractionHandleAxisWorld(type, index, axis_World);
  if (axis_World[0] == 0.0 && axis_World[1] == 0.0 && axis_World[2] == 0.0)
    {
    // No axis specified. The handle should be viewable from any direction.
    return opacity;
    }

  double viewNormal_World[3] = { 0.0, 0.0, 0.0 };
  this->GetHandleToCameraVector(viewNormal_World);
  if (vtkMath::Dot(viewNormal_World, axis_World) < 0.0)
    {
    vtkMath::MultiplyScalar(axis_World, -1.0);
    }

  double fadeAngleRange = this->StartFadeAngleDegrees - this->EndFadeAngleDegrees;
  double angle = vtkMath::DegreesFromRadians(vtkMath::AngleBetweenVectors(viewNormal_World, axis_World));
  if (type == InteractionRotationHandle)
    {
    // Fade happens when the axis approaches 90 degrees from the view normal
    if (angle > 90.0 - this->EndFadeAngleDegrees)
      {
      opacity = 0.0;
      }
    else if (angle > 90.0 - this->StartFadeAngleDegrees)
      {
      double difference = angle - (90.0 - this->StartFadeAngleDegrees);
      opacity = 1.0 - (difference / fadeAngleRange);
      }
    }
  else if (type == InteractionTranslationHandle || type == InteractionScaleHandle)
    {
    // Fade happens when the axis approaches 0 degrees from the view normal
    if (angle < this->EndFadeAngleDegrees)
      {
      opacity = 0.0;
      }
    else if (angle < this->StartFadeAngleDegrees)
      {
      double difference = angle - this->EndFadeAngleDegrees;
      opacity = (difference / fadeAngleRange);
      }
    }
  return opacity;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetHandleToCameraVector(double normal[3])
{
  if (!normal)
    {
    return;
    }

  if (this->GetSliceNode())
    {
    double viewPlaneNormal4[4] = { 0, 0, 1, 0 };
    vtkMRMLSliceNode* sliceNode = vtkMRMLSliceNode::SafeDownCast(this->GetViewNode());
    if (sliceNode)
      {
      sliceNode->GetSliceToRAS()->MultiplyPoint(viewPlaneNormal4, viewPlaneNormal4);
      }
    normal[0] = viewPlaneNormal4[0];
    normal[1] = viewPlaneNormal4[1];
    normal[2] = viewPlaneNormal4[2];
    }
  else if (this->GetRenderer() && this->GetRenderer()->GetActiveCamera())
    {
    vtkCamera* camera = this->GetRenderer()->GetActiveCamera();
    if (camera->GetParallelProjection())
      {
      camera->GetViewPlaneNormal(normal);
      }
    else
      {
      camera->GetPosition(normal);
      vtkMath::Subtract(normal, this->GetHandleToWorldTransform()->TransformPoint(0, 0, 0), normal);
      vtkMath::Normalize(normal);
      }
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::SetWidgetScale(double scale)
{
  if (this->WidgetScale == scale)
    {
    return;
    }
  this->WidgetScale = scale;
  this->NeedToRenderOn();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandleOriginWorld(double originWorld[3])
{
  if (!originWorld)
    {
    return;
    }

  double handleOrigin[3] = { 0,0,0 };
  this->Pipeline->HandleToWorldTransform->TransformPoint(handleOrigin, originWorld);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandleAxisLocal(int type, int index, double axisLocal[3])
{
  if (!axisLocal)
    {
    vtkErrorWithObjectMacro(nullptr, "GetInteractionHandleVectorWorld: Invalid axis argument");
    return;
    }

  axisLocal[0] = 0.0;
  axisLocal[1] = 0.0;
  axisLocal[2] = 0.0;

  switch (index)
    {
    case 0:
      axisLocal[0] = 1.0;
      break;
    case 1:
      axisLocal[1] = 1.0;
      break;
    case 2:
      axisLocal[2] = 1.0;
      break;
    default:
      break;
    }
}


//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandleAxisWorld(int type, int index, double axisWorld[3])
{
  if (!axisWorld)
    {
    vtkErrorWithObjectMacro(nullptr, "GetInteractionHandleVectorWorld: Invalid axis argument");
    return;
    }

  double origin[3] = { 0.0, 0.0, 0.0 };
  this->GetInteractionHandleAxisLocal(type, index, axisWorld);
  this->Pipeline->HandleToWorldTransform->TransformVectorAtPoint(origin, axisWorld, axisWorld);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandlePositionLocal(int type, int index, double positionLocal[3])
{
  if (!positionLocal)
    {
    vtkErrorMacro("GetInteractionHandlePositionLocal: Invalid position argument");
    return;
    }

  vtkPolyData* handlePolyData = this->GetHandlePolydata(type);
  if (!handlePolyData)
    {
    return;
    }

  if (index < 0 || index >= handlePolyData->GetNumberOfPoints())
    {
    return;
    }

  handlePolyData->GetPoint(index, positionLocal);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandlePositionWorld(int type, int index, double positionWorld[3])
{
  if (!positionWorld)
    {
    vtkErrorWithObjectMacro(nullptr, "GetInteractionHandlePositionWorld: Invalid position argument");
    }

  vtkPolyData* handlePolyData = this->GetHandlePolydata(type);
  if (!handlePolyData)
    {
    return;
    }
  handlePolyData->GetPoint(index, positionWorld);

  vtkNew<vtkTransform> handleScaleTransform;
  handleScaleTransform->Scale(this->WidgetScale, this->WidgetScale, this->WidgetScale);
  handleScaleTransform->TransformPoint(positionWorld, positionWorld);

  this->Pipeline->HandleToWorldTransform->TransformPoint(positionWorld, positionWorld);
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::HandleInfo vtkMRMLInteractionWidgetRepresentation::GetHandleInfo(int type, int index)
{
  double handlePositionLocal[3] = { 0.0, 0.0, 0.0 };
  this->GetInteractionHandlePositionLocal(type, index, handlePositionLocal);

  double handlePositionWorld[3] = { 0.0, 0.0, 0.0 };
  this->GetInteractionHandlePositionWorld(type, index, handlePositionWorld);

  double color[4] = { 0.0, 0.0, 0.0, 0.0 };
  this->GetHandleColor(type, index, color);

  int glyphType = this->GetHandleGlyphType(type, index);

  return HandleInfo(index, type, handlePositionWorld, handlePositionLocal, color, glyphType);
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::HandleInfoList vtkMRMLInteractionWidgetRepresentation::GetHandleInfoList()
{
  HandleInfoList handleInfoList;

  for (int index = 0; index < this->GetNumberOfHandles(InteractionRotationHandle); ++index)
    {
    handleInfoList.push_back(this->GetHandleInfo(InteractionRotationHandle, index));
    }

  for (int index = 0; index < this->GetNumberOfHandles(InteractionTranslationHandle); ++index)
    {
    handleInfoList.push_back(this->GetHandleInfo(InteractionTranslationHandle, index));
    }

  for (int index = 0; index < this->GetNumberOfHandles(InteractionScaleHandle); ++index)
    {
    handleInfoList.push_back(this->GetHandleInfo(InteractionScaleHandle, index));
    }

  return handleInfoList;
}

//----------------------------------------------------------------------
vtkMRMLSliceNode* vtkMRMLInteractionWidgetRepresentation::GetSliceNode()
{
  return vtkMRMLSliceNode::SafeDownCast(this->ViewNode);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetSliceToWorldCoordinates(const double slicePos[2],
  double worldPos[3])
{
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (!this->Renderer || !sliceNode)
    {
    return;
    }

  double xyzw[4] =
    {
    slicePos[0] - this->Renderer->GetOrigin()[0],
    slicePos[1] - this->Renderer->GetOrigin()[1],
    0.0,
    1.0
    };
  double rasw[4] = { 0.0, 0.0, 0.0, 1.0 };

  this->GetSliceNode()->GetXYToRAS()->MultiplyPoint(xyzw, rasw);

  worldPos[0] = rasw[0] / rasw[3];
  worldPos[1] = rasw[1] / rasw[3];
  worldPos[2] = rasw[2] / rasw[3];
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateSlicePlaneFromSliceNode()
{
  if (!this->GetSliceNode())
    {
    return;
    }

  vtkMatrix4x4* sliceXYToRAS = this->GetSliceNode()->GetXYToRAS();

  if (this->Pipeline)
    {
    // Update transformation to slice
    vtkNew<vtkMatrix4x4> rasToXY;
    rasToXY->DeepCopy(sliceXYToRAS);
    //rasToXY->Invert();
    this->Pipeline->WorldToSliceTransform->Identity();
    this->Pipeline->WorldToSliceTransform->Concatenate(rasToXY);
    }

  // Update slice plane (for distance computation)
  double normal[3] = { 0.0, 0.0, 0.0 };
  double origin[3] = { 0.0, 0.0, 0.0 };
  const double planeOrientation = 1.0; // +/-1: orientation of the normal
  for (int i = 0; i < 3; i++)
    {
    normal[i] = planeOrientation * sliceXYToRAS->GetElement(i, 2);
    origin[i] = sliceXYToRAS->GetElement(i, 3);
    }
  vtkMath::Normalize(normal);

  // Compare slice normal and new normal
  double normalDifferenceAngle = vtkMath::AngleBetweenVectors(normal, this->SlicePlane->GetNormal());
  double originDifferenceMm = vtkMath::Distance2BetweenPoints(origin, this->SlicePlane->GetOrigin());
  double epsilon = 1e-6;
  if (normalDifferenceAngle < epsilon && originDifferenceMm < epsilon)
    {
    // No change in slice plane
    return;
    }

  this->SlicePlane->SetNormal(normal);
  this->SlicePlane->SetOrigin(origin);
  this->SlicePlane->Modified();
  this->NeedToRenderOn();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateViewScaleFactor()
{
  this->ViewScaleFactorMmPerPixel = 1.0;
  this->ScreenSizePixel = 1000.0;
  if (!this->Renderer || !this->Renderer->GetActiveCamera())
    {
    return;
    }

  int* screenSize = this->Renderer->GetRenderWindow()->GetScreenSize();
  double screenSizePixel = sqrt(screenSize[0] * screenSize[0] + screenSize[1] * screenSize[1]);
  if (screenSizePixel < 1.0)
    {
    // render window is not fully initialized yet
    return;
    }
  this->ScreenSizePixel = screenSizePixel;

  if (this->GetSliceNode())
    {
    vtkMatrix4x4* xyToSlice = this->GetSliceNode()->GetXYToSlice();
    this->ViewScaleFactorMmPerPixel = sqrt(xyToSlice->GetElement(0, 1) * xyToSlice->GetElement(0, 1)
      + xyToSlice->GetElement(1, 1) * xyToSlice->GetElement(1, 1));
    }
  else
    {
    double cameraFP[3] = { 0.0, 0.0, 0.0 };
    this->Renderer->GetActiveCamera()->GetFocalPoint(cameraFP);
    this->ViewScaleFactorMmPerPixel = this->GetViewScaleFactorAtPosition(cameraFP);
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateHandleSize()
{
  if (!this->GetInteractionSizeAbsolute())
    {
    this->InteractionSize = this->ScreenSizePixel * this->ScreenScaleFactor
      * this->GetInteractionScale() / 100.0 * this->ViewScaleFactorMmPerPixel;
    }
  else
    {
    this->InteractionSize = this->GetInteractionSize() / this->ViewScaleFactorMmPerPixel;
    }
  this->SetWidgetScale(this->InteractionSize);
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetInteractionScale()
{
  return 3.0;
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetInteractionSize()
{
  return 1.0;
}

//----------------------------------------------------------------------
bool vtkMRMLInteractionWidgetRepresentation::GetInteractionSizeAbsolute()
{
  return false;
}

//----------------------------------------------------------------------
vtkTransform* vtkMRMLInteractionWidgetRepresentation::GetHandleToWorldTransform()
{
  if (!this->Pipeline)
    {
    return nullptr;
    }
  return this->Pipeline->HandleToWorldTransform;
}
