<?php

/**
 * @file
 * An primary Solr display for running XQuery queries.
 * @todo
 *   Remind the user that Solr may not be up to date.
 */

/**
 * Extention of IslandoraSolrResults to create an alternative display type.
 */
class IslandoraSolrResultsStamp extends IslandoraSolrResults {

  /**
   * Constructor.
   */
  public function __construct() {
    parent::__construct();
  }

  /**
   * Renders the Solr results as a table.
   *
   * @see displayResults()
   *
   * @param array $results
   *   The Solr results from IslandoraSolrQueryProcessor::solrResult
   *
   * @return array
   *   Rendered Solr results
   */
  public function printResults($results) {
    if (module_exists('islandora_object_lock')) {
      module_load_include('inc', 'islandora_object_lock', 'includes/utilities');
      $locked = islandora_object_lock_get_locks(0, 10000);
      $found_pids = array();
      foreach ($results['response']['objects'] as $object) {
        $found_pids[] = $object['PID'];
      }
      $conflicts = array_intersect(array_keys($locked), $found_pids);
      if (!empty($conflicts)) {
        $output = count($conflicts) == 1 ? t('This object in the resultset is currently locked by other users') : t('These objects in the resultset are currently locked by other users');

        foreach ($conflicts as $conflict) {
          $output .= '<br />' . l($conflict, "islandora/object/$conflict");
        }
        return $output;
      }
    }
    if (empty($results)) {
      return t('No results.');
    }
    $results_form = drupal_get_form('cwrc_dashboards_results_form', $results['response']['objects'], $this);
    return drupal_render($results_form);
  }

}

/**
 * Form building function.
 *
 * @param array $form
 *   The Drupal form definition.
 * @param array $form_state
 *   The Drupal form state.
 * @param array $object_results
 *   The object results from the Islandora Solr processor created in the
 *   printResults function.
 * @param IslandoraSolrResultsXquery $solr
 *   Solr results object, used in submit handler.
 *
 * @return array
 *   An array containing the form structure.
 */
function cwrc_dashboards_results_form($form, &$form_state, $object_results, $solr) {
  module_load_include('inc', 'islandora_workflow_rest', 'includes/cwrc_workflow');
  module_load_include('inc', 'islandora_workflow_rest', 'includes/workflow.form');
  module_load_include('inc', 'islandora_workflow_rest', 'includes/basic_enum');
  module_load_include('inc', 'islandora_workflow_rest', 'includes/utilities');

  // This form gets called in a roundabout fashion and needs to include itself.
  form_load_include($form_state, 'inc', 'cwrc_dashboards', 'includes/stamp_solr_results');

  $header = array('markup' => t('Object'));
  $rows = array();
  $label_field = variable_get('islandora_solr_object_label_field', 'fgs_label_s');
  foreach ($object_results as $object_result) {
    $rows[$object_result['PID']] = array(
      'markup' => l(
          isset($object_result['solr_doc'][$label_field]) ? $object_result['solr_doc'][$label_field] : $object_result['PID'], $object_result['object_url']
      ),
      'pid' => $object_result['PID'],
    );
  }
  $workflow = NULL;

  // TRUE because struggling with ajax isn't worth it.
  $form['#tree'] = TRUE;
  $form['islandora_object_table'] = array(
    '#header' => $header,
    '#type' => 'tableselect',
    '#title' => t('Objects'),
    '#options' => $rows,
    '#attributes' => array('class' => array('islandora-stamp-solr-results')),
    '#empty' => t('No results found.'),
  );

  $headers = array(
    'select' => '',
    'stamp' => t('Name'),
    'descr' => t('Description'),
    'category' => t('Category'),
    'status' => t('Status'),
  );

  // Start building the stamp table.
  $form['islandora_stamp_table'] = array(
    '#theme' => 'table',
    '#rows' => array(),
    '#header' => $headers,
    '#tree' => TRUE,
    '#empty' => t('There are no stamps.'),
    'rows' => array(
      '#tree' => TRUE,
    ),
  );

  // Build category options.
  $categories = array_merge(
    array(NULL => t('Select')),
    drupal_map_assoc(array_values(AssignedCategory::getAllConstants()))
  );

  // Build status options.
  $statuses = array(
    NULL => t('Select'),
    'c' => t('Complete'),
    'ipr' => t('In Progress'),
    'p' => t('Pending'),
    'i' => t('Incomplete'),
  );

  // Iterate over workflow stamps and build form-based table.
  foreach (islandora_workflow_rest_get_all_stamps(1000) as $stamp) {
    $identifier = drupal_clean_css_identifier($stamp->stamp);
    $select = array(
      '#id' => 'select-' . $identifier,
      '#type' => 'checkbox',
      '#attributes' => array(
        'class' => array('stamp-select'),
      ),
    );

    $category = array(
      '#id' => 'category-' . $identifier,
      '#type' => 'select',
      '#title' => t('Category'),
      '#options' => $categories,
      '#states' => array(
        'visible' => array(
          '#select-' . $identifier => array('checked' => TRUE),
        ),
      ),
    );

    $status = array(
      '#id' => 'status-' . $identifier,
      '#type' => 'select',
      '#title' => t('Status'),
      '#options' => $statuses,
      '#states' => array(
        'visible' => array(
          '#select-' . $identifier => array('checked' => TRUE),
        ),
      ),
    );

    $form['islandora_stamp_table']['#rows'][] = array(
      'select' => array('data' => &$select),
      'stamp' => array('data' => $stamp->stamp),
      'descr' => array('data' => $stamp->descr),
      'category' => array('data' => &$category),
      'status' => array('data' => &$status),
    );

    // Sneaky way to make embedded form elements work as expected.
    $form['islandora_stamp_table']['rows'][$stamp->stamp] = array(
      'select' => &$select,
      'status' => &$status,
      'category' => &$category,
    );

    unset($select);
    unset($status);
    unset($category);
  }

  if (count($rows) > 0) {
    $form['stamp_controls'] = array(
      'submit_selected' => array(
        '#type' => 'submit',
        '#value' => t('Run Selected'),
      ),
    );
  }

  return $form;
}

/**
 * Ajax callback to render inputs when XQuery changes.
 */
function cwrc_dashboards_xquery_inputs_ajax($form, $form_state) {
  return $form['stamp_controls']['stamp_inputs'];
}

/**
 * Form validation callback, ensures status and category are filled out.
 */
function cwrc_dashboards_results_form_validate(array $form, array &$form_state) {
  // Iterate over rows from table.
  foreach ($form_state['values']['islandora_stamp_table']['rows'] as $stamp => $value) {
    if ($value['select'] == 1) {
      // Status should have a value.
      if (empty($value['status'])) {
        form_set_error(
          'islandora_stamp_table][rows][' . $stamp . '][status',
          t('You must select a status for "@stamp".', array('@stamp' => $stamp))
        );
      }

      // Category should have a value.
      if (empty($value['category'])) {
        form_set_error(
          'islandora_stamp_table][rows][' . $stamp . '][category',
          t('You must select a category for "@stamp".', array('@stamp' => $stamp))
        );
      }
    }
  }
}

/**
 * Submit handler for the islandora_xquery_results form.
 *
 * @param array $form
 *   The Drupal form definition.
 * @param array $form_state
 *   The Drupal form state.
 */
function cwrc_dashboards_results_form_submit(array $form, array &$form_state) {
  module_load_include('inc', 'islandora_workflow_rest', 'includes/basic_enum');
  module_load_include('inc', 'islandora_workflow_rest', 'includes/utilities');
  module_load_include('inc', 'islandora_workflow_rest', 'includes/workflow_item');

  // Get values and filter PIDs.
  $values = $form_state['values'];
  $pids = array_filter($values['islandora_object_table']);
  $stamps = array_filter($values['islandora_stamp_table']['rows'], function ($row) {
    return ($row['select'] == 1);
  });

  // Iterate over selected PIDs.
  foreach ($pids as $pid) {

    // Load object and the workflow datastream.
    $object = islandora_object_load($pid);
    if (!isset($object['WORKFLOW'])) {
      // Create the workflow datastream if required.
      $workflow = CWRCWorkflowAPI::fromTemplate();
      CWRCWorkflowAPI::updateDatastream($workflow, $object);
    }
    else {
      $workflow = CWRCWorkflowAPI::fromDatastream($object['WORKFLOW']);
    }

    // Iterate over stamps selected, applying to workflow datastream.
    foreach ($stamps as $id => $stamp) {

      // Setup params for applying the stamp.
      $activity_params['stamp'] = $id;
      $activity_params['category'] = $stamp['category'];
      $activity_params['status'] = $stamp['status'];

      // Build the workflow stamp to apply.
      $activity = new WorkflowItem(WorkflowConst::ACTIVITY, $activity_params);
      $entry = new WorkflowItem(WorkflowConst::WORKFLOW, array('toolID' => ""));
      $workflow->addWorkflowStep($object->id, $entry, $activity, NULL);

      // Apply the stamp.
      CWRCWorkflowAPI::updateDatastream($workflow, $object);
    }
  }
  drupal_set_message(t('Workflow stamps have been applied to the objects'), 'status', FALSE);
}
