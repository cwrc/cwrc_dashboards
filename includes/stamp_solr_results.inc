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

  $options = array();

  $results = islandora_workflow_rest_get_all_stamps(1000);

  while ($data = $results->fetchObject()) {
    $options[$data->stamp]['stamp'] = $data->stamp;
    $options[$data->stamp]['descr'] = $data->descr;
  }

  $header = array(
    'stamp' => t('Name'),
    'descr' => t('Description'),
  );

  $form['islandora_stamp_table'] = array(
    '#type' => 'tableselect',
    '#title' => t('Stamps'),
    '#header' => $header,
    '#options' => $options,
    '#attributes' => array(),
    '#empty' => t("There are no stamps."),
  );

  $assigned_categories = drupal_map_assoc(array_values(AssignedCategory::getAllConstants()));
  $assigned_categories[NULL] = "Select";

  $form['islandora_categories_table'] = array(
    '#type' => 'select',
    '#title' => t('Category'),
    '#header' => $header,
    '#required' => TRUE,
    '#options' => $assigned_categories,
  );

  $status_arr = array(
    NULL => t('Select'),
    'c' => t('Complete'),
    'ipr' => t('In Progress'),
    'p' => t('Pending'),
    'i' => t('Incomplete'),
  );

  $form['islandora_status_table'] = array(
    '#type' => 'select',
    '#title' => t('Status'),
    '#required' => TRUE,
    '#options' => $status_arr,
  );

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

  if(!empty($form['islandora_object_table']['#value'])) {
    $pids = $form['islandora_object_table']['#value'];
  } else {
    $pids = NULL;
  }

  if(!empty($form['islandora_stamp_table']['#value'])) {
    $stamps = $form['islandora_stamp_table']['#value'];
  } else {
    $stamps = NULL;
  }

  $category = $form['islandora_categories_table']['#value'];
  $status = $form['islandora_status_table']['#value'];

  foreach ($pids as $pid) {
    $object = islandora_object_load($pid);

    if (!isset($object['WORKFLOW'])) {
      $workflow = CWRCWorkflowAPI::fromTemplate();
      CWRCWorkflowAPI::updateDatastream($workflow, $object);
    }
    else {
      $workflow = CWRCWorkflowAPI::fromDatastream($object['WORKFLOW']);
    }
    foreach ($stamps as $sid => $stamp) {

      $activity_params = array();

      $activity_params['category'] = $category;
      $activity_params['stamp'] = $stamps[$sid];
      $activity_params['status'] = $status;

      $activity = new WorkflowItem(WorkflowConst::ACTIVITY, $activity_params);

      $entry = new WorkflowItem(WorkflowConst::WORKFLOW, array('toolID' => ""));
      $last_workflow = $workflow->entryToArray();
      if ($last_workflow) {
        $last_wf_string = islandora_workflow_rest_prepare_workflow_string($last_workflow, "previous");
      }
      else {
        $last_wf_string = "";
      }
      $workflow->addWorkflowStep($object->id, $entry, $activity, NULL);

      CWRCWorkflowAPI::updateDatastream($workflow, $object);
    }
  }
  drupal_set_message(t('Workflow stamps have been applied to the objects'), 'status', FALSE);
}